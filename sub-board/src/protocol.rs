use crate::motor::{Dir, MotorCommand};
use stm32f1xx_hal::{
    gpio::{Alternate, Floating, Input, Pin, PushPull},
    pac::USART1,
    rcc::Rcc,
};

enum State {
    Idle,
    WaitingByte2,
    WaitingFlag,
    WaitingChecksum,
    WaitingEndE,
    WaitingEndD,
}

pub struct Protocol {
    usart: USART1,
    _tx: Pin<'A', 9, Alternate<PushPull>>,
    _rx: Pin<'A', 10, Input<Floating>>,
    state: State,
    flag: u8,
    checksum: u8,
}

impl Protocol {
    pub fn new(
        usart: USART1,
        tx_pin: Pin<'A', 9, Alternate<PushPull>>,
        rx_pin: Pin<'A', 10, Input<Floating>>,
        rcc: &mut Rcc,
    ) -> Self {
        // Enable USART1 clock on APB2
        rcc.apb2enr().modify(|_, w| w.usart1en().set_bit());

        // BRR for 115200 @ 64 MHz PCLK2: round(64_000_000 / 115_200) = 556 = 0x022C
        usart.brr().write(|w| unsafe { w.bits(0x022C) });
        // 8-N-1, UE=1, TE=1, RE=1
        usart.cr1().write(|w| w.ue().set_bit().te().set_bit().re().set_bit());

        Self {
            usart,
            _tx: tx_pin,
            _rx: rx_pin,
            state: State::Idle,
            flag: 0,
            checksum: 0,
        }
    }

    pub fn next_command(&mut self) -> Result<MotorCommand, ()> {
        loop {
            let sr = self.usart.sr().read();
            if sr.ore().bit_is_set() || sr.fe().bit_is_set() {
                let _ = self.usart.dr().read();
                continue;
            }
            if sr.rxne().bit_is_set() {
                let byte = self.usart.dr().read().dr().bits() as u8;
                if let Some(result) = self.feed(byte) {
                    return result;
                }
            }
        }
    }

    fn feed(&mut self, byte: u8) -> Option<Result<MotorCommand, ()>> {
        match self.state {
            State::Idle => {
                if byte == b'S' {
                    self.state = State::WaitingByte2;
                }
            }
            State::WaitingByte2 => {
                self.state = if byte == b'T' {
                    self.checksum = b'S' ^ b'T';
                    State::WaitingFlag
                } else {
                    State::Idle
                };
            }
            State::WaitingFlag => {
                self.flag = byte;
                self.checksum ^= byte;
                self.state = State::WaitingChecksum;
            }
            State::WaitingChecksum => {
                self.checksum ^= byte;
                self.state = State::WaitingEndE;
            }
            State::WaitingEndE => {
                self.state = if byte == b'E' {
                    State::WaitingEndD
                } else {
                    State::Idle
                };
            }
            State::WaitingEndD => {
                self.state = State::Idle;
                if byte == b'D' {
                    if self.checksum == 0 {
                        self.write(b"OK");
                        return Some(Ok(MotorCommand {
                            step_motor: Self::decode(self.flag >> 6),
                            left_motor: Self::decode(self.flag >> 4),
                            right_motor: Self::decode(self.flag >> 2),
                        }));
                    }
                    self.write(b"Err");
                    return Some(Err(()));
                }
            }
        }
        None
    }

    fn write(&mut self, data: &[u8]) {
        for &b in data {
            while self.usart.sr().read().txe().bit_is_clear() {}
            self.usart.dr().write(|w| unsafe { w.dr().bits(b as u16) });
        }
        while self.usart.sr().read().tc().bit_is_clear() {}
    }

    fn decode(bits: u8) -> Dir {
        match bits & 0b11 {
            0b10 => Dir::Forward,
            0b01 => Dir::Backward,
            _ => Dir::Stop,
        }
    }
}

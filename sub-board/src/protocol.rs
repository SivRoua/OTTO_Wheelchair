use crate::motor::{Dir, MotorCommand};
use stm32f1xx_hal::{
    gpio::{Alternate, OpenDrain, Pin},
    pac::I2C1,
    rcc::Rcc,
};

const SLAVE_ADDR: u8 = 0x42;

pub struct Protocol {
    i2c: I2C1,
    _scl: Pin<'B', 6, Alternate<OpenDrain>>,
    _sda: Pin<'B', 7, Alternate<OpenDrain>>,
}

impl Protocol {
    pub fn new(
        i2c: I2C1,
        scl: Pin<'B', 6, Alternate<OpenDrain>>,
        sda: Pin<'B', 7, Alternate<OpenDrain>>,
        rcc: &mut Rcc,
    ) -> Self {
        rcc.apb1enr().modify(|_, w| w.i2c1en().set_bit());
        rcc.apb1rstr().modify(|_, w| w.i2c1rst().set_bit());
        rcc.apb1rstr().modify(|_, w| w.i2c1rst().clear_bit());

        i2c.cr2().write(|w| unsafe { w.freq().bits(32) }); // PCLK1 = 32 MHz
        i2c.oar1().write(|w| unsafe { w.bits((SLAVE_ADDR as u16) << 1) });
        i2c.cr1().write(|w| w.pe().set_bit().ack().set_bit());

        Self { i2c, _scl: scl, _sda: sda }
    }

    pub fn next_command(&mut self) -> Result<MotorCommand, ()> {
        loop {
            // Wait for address match, then clear ADDR (read SR1 then SR2)
            while self.i2c.sr1().read().addr().bit_is_clear() {}
            let _ = self.i2c.sr1().read();
            let _ = self.i2c.sr2().read();

            // Receive 6-byte frame
            let mut frame = [0u8; 6];
            for byte in frame.iter_mut() {
                while self.i2c.sr1().read().rx_ne().bit_is_clear() {}
                *byte = self.i2c.dr().read().bits() as u8;
            }

            // Wait for STOP, then clear STOPF (read SR1, write CR1)
            while self.i2c.sr1().read().stopf().bit_is_clear() {}
            let _ = self.i2c.sr1().read();
            self.i2c.cr1().write(|w| w.pe().set_bit().ack().set_bit());

            // Validate: S T flag cs E D
            if frame[0] == b'S'
                && frame[1] == b'T'
                && frame[4] == b'E'
                && frame[5] == b'D'
                && b'S' ^ b'T' ^ frame[2] ^ frame[3] == 0
            {
                return Ok(MotorCommand {
                    step_motor: Self::decode(frame[2] >> 6),
                    left_motor: Self::decode(frame[2] >> 4),
                    right_motor: Self::decode(frame[2] >> 2),
                });
            } else {
                return Err(());
            }
        }
    }

    fn decode(bits: u8) -> Dir {
        match bits & 0b11 {
            0b10 => Dir::Forward,
            0b01 => Dir::Backward,
            _ => Dir::Stop,
        }
    }
}

use crate::motor::{Dir, MotorCommand};
use stm32f1xx_hal::{
    gpio::{Alternate, OpenDrain, Pin},
    pac::I2C1,
    rcc::Rcc,
};

const SLAVE_ADDR: u8 = 0x42;

// PCLK1=32MHz, standard mode 100kHz: CCR=160, TRISE=33
const I2C_CCR: u16 = 160;
const I2C_TRISE: u8 = 33;

// Spin-loop iteration budget (~32 MHz, ~3 cycles/iter ≈ 10 ms)
const TIMEOUT_TICKS: u32 = 100_000;

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

        i2c.cr2().write(|w| unsafe { w.freq().bits(32) });
        i2c.ccr().write(|w| unsafe { w.ccr().bits(I2C_CCR) });
        i2c.trise().write(|w| unsafe { w.trise().bits(I2C_TRISE) });
        i2c.oar1().write(|w| unsafe { w.bits((SLAVE_ADDR as u16) << 1) });
        i2c.cr1().modify(|_, w| w.pe().set_bit().ack().set_bit());

        Self { i2c, _scl: scl, _sda: sda }
    }

    pub fn next_command(&mut self) -> Result<MotorCommand, ()> {
        // Wait for address match (no timeout — idle state)
        while self.i2c.sr1().read().addr().bit_is_clear() {}
        // Clear ADDR by reading SR1 then SR2
        let _ = self.i2c.sr1().read();
        let _ = self.i2c.sr2().read();

        // Receive 6-byte frame
        let mut frame = [0u8; 6];
        for byte in frame.iter_mut() {
            self.wait_flag(|sr1| sr1.rx_ne().bit_is_set())?;
            *byte = self.i2c.dr().read().bits() as u8;
        }

        // Wait for STOP, then clear STOPF (read SR1, modify CR1)
        self.wait_flag(|sr1| sr1.stopf().bit_is_set())?;
        let _ = self.i2c.sr1().read();
        self.i2c.cr1().modify(|_, w| w.pe().set_bit().ack().set_bit());

        // Validate: S T flag cs E D
        if frame[0] == b'S'
            && frame[1] == b'T'
            && frame[4] == b'E'
            && frame[5] == b'D'
            && b'S' ^ b'T' ^ frame[2] ^ frame[3] == 0
        {
            Ok(MotorCommand {
                step_motor: Self::decode(frame[2] >> 6),
                left_motor:  Self::decode(frame[2] >> 4),
                right_motor: Self::decode(frame[2] >> 2),
            })
        } else {
            Err(())
        }
    }

    // Poll `pred(sr1)` with a timeout; returns Err(()) and resets the peripheral on expiry.
    fn wait_flag<F>(&mut self, pred: F) -> Result<(), ()>
    where
        F: Fn(stm32f1xx_hal::pac::i2c1::sr1::R) -> bool,
    {
        let mut ticks = 0u32;
        loop {
            if pred(self.i2c.sr1().read()) {
                return Ok(());
            }
            ticks += 1;
            if ticks >= TIMEOUT_TICKS {
                // Reset PE to flush any in-progress transaction, then re-enable.
                self.i2c.cr1().modify(|_, w| w.pe().clear_bit());
                self.i2c.cr1().modify(|_, w| w.pe().set_bit().ack().set_bit());
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

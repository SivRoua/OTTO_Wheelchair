mod tb6612;
mod uln2003;

use tb6612::TB6612;
use uln2003::ULN2003;

use stm32f1xx_hal::gpio::{Output, Pin, PushPull};

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Dir {
    Stop,
    Forward,
    Backward,
}

pub struct MotorCommand {
    pub step_motor: Dir,
    pub left_motor: Dir,
    pub right_motor: Dir,
}

pub struct Motors {
    stepper: ULN2003,
    motor_a: TB6612<14, 15>, // Left,  PB14(AIN1) PB15(AIN2)
    motor_b: TB6612<13, 12>, // Right, PB13(BIN1) PB12(BIN2)
}

impl Motors {
    pub fn new(
        // ULN2003 IN1..IN4 (PA0..PA3)
        pa0: Pin<'A', 0, Output<PushPull>>,
        pa1: Pin<'A', 1, Output<PushPull>>,
        pa2: Pin<'A', 2, Output<PushPull>>,
        pa3: Pin<'A', 3, Output<PushPull>>,
        // Motor A (PB14 AIN1, PB15 AIN2)
        a_in1: Pin<'B', 14, Output<PushPull>>,
        a_in2: Pin<'B', 15, Output<PushPull>>,
        // Motor B (PB13 BIN1, PB12 BIN2)
        b_in1: Pin<'B', 13, Output<PushPull>>,
        b_in2: Pin<'B', 12, Output<PushPull>>,
    ) -> Self {
        Self {
            stepper: ULN2003::new(pa0, pa1, pa2, pa3),
            motor_a: TB6612::new(a_in1, a_in2),
            motor_b: TB6612::new(b_in1, b_in2),
        }
    }

    pub fn apply(&mut self, cmd: &MotorCommand) {
        match cmd.step_motor {
            Dir::Forward => self.stepper.forward(),
            Dir::Backward => self.stepper.backward(),
            Dir::Stop => self.stepper.release(),
        }

        match cmd.left_motor {
            Dir::Forward => self.motor_a.forward(),
            Dir::Backward => self.motor_a.backward(),
            Dir::Stop => self.motor_a.coast(),
        }

        match cmd.right_motor {
            Dir::Forward => self.motor_b.forward(),
            Dir::Backward => self.motor_b.backward(),
            Dir::Stop => self.motor_b.coast(),
        }
    }

    pub fn stop_all(&mut self) {
        self.motor_a.coast();
        self.motor_b.coast();
    }
}

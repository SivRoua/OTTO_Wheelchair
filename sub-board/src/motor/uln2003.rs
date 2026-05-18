use stm32f1xx_hal::gpio::{Output, Pin, PinState, PushPull};

const SEQ: [u8; 4] = [
    0b0011, // IN1=1 IN2=1 IN3=0 IN4=0
    0b0110, // IN1=0 IN2=1 IN3=1 IN4=0
    0b1100, // IN1=0 IN2=0 IN3=1 IN4=1
    0b1001, // IN1=1 IN2=0 IN3=0 IN4=1
];

pub struct ULN2003 {
    in1: Pin<'A', 0, Output<PushPull>>,
    in2: Pin<'A', 1, Output<PushPull>>,
    in3: Pin<'A', 2, Output<PushPull>>,
    in4: Pin<'A', 3, Output<PushPull>>,
    step: u8,
    hold_tick: u8,
}

impl ULN2003 {
    pub fn new(
        in1: Pin<'A', 0, Output<PushPull>>,
        in2: Pin<'A', 1, Output<PushPull>>,
        in3: Pin<'A', 2, Output<PushPull>>,
        in4: Pin<'A', 3, Output<PushPull>>,
    ) -> Self {
        Self {
            in1,
            in2,
            in3,
            in4,
            step: 0,
            hold_tick: 0,
        }
    }

    pub fn forward(&mut self) {
        self.step = (self.step + 1) % 4;
        self.write(SEQ[self.step as usize]);
    }

    pub fn backward(&mut self) {
        self.step = (self.step + 3) % 4; // -1 mod 4
        self.write(SEQ[self.step as usize]);
    }

    pub fn hold(&mut self) {
        self.hold_tick = self.hold_tick.wrapping_add(1);
        if self.hold_tick % 2 == 0 {
            self.write(SEQ[self.step as usize]);
        } else {
            self.write(0x00);
        }
    }

    pub fn release(&mut self) {
        self.write(0x00);
    }

    fn write(&mut self, v: u8) {
        self.in1.set_state(pin_state(v, 0));
        self.in2.set_state(pin_state(v, 1));
        self.in3.set_state(pin_state(v, 2));
        self.in4.set_state(pin_state(v, 3));
    }
}

fn pin_state(v: u8, bit: u8) -> PinState {
    if (v >> bit) & 1 != 0 {
        PinState::High
    } else {
        PinState::Low
    }
}

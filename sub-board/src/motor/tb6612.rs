use stm32f1xx_hal::gpio::{Output, Pin, PushPull};

pub struct TB6612<const N1: u8, const N2: u8> {
    in1: Pin<'B', N1, Output<PushPull>>,
    in2: Pin<'B', N2, Output<PushPull>>,
}

impl<const N1: u8, const N2: u8> TB6612<N1, N2> {
    pub fn new(
        in1: Pin<'B', N1, Output<PushPull>>,
        in2: Pin<'B', N2, Output<PushPull>>,
    ) -> Self {
        Self { in1, in2 }
    }

    pub fn forward(&mut self) {
        self.in1.set_high();
        self.in2.set_low();
    }

    pub fn backward(&mut self) {
        self.in1.set_low();
        self.in2.set_high();
    }

    pub fn brake(&mut self) {
        self.in1.set_high();
        self.in2.set_high();
    }

    pub fn coast(&mut self) {
        self.in1.set_low();
        self.in2.set_low();
    }
}

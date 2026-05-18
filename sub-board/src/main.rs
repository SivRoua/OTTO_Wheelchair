#![no_std]
#![no_main]

mod motor;
mod protocol;

use cortex_m_rt::entry;
use defmt::info;
use motor::Motors;
use protocol::Protocol;
use stm32f1xx_hal::{
    gpio::PinState,
    pac,
    prelude::*,
    rcc::Config as RccConfig,
    time::U32Ext,
};
use {defmt_rtt as _, panic_probe as _};

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();
    let cp = cortex_m::Peripherals::take().unwrap();

    let mut flash = dp.FLASH.constrain();
    let rcc = dp.RCC.constrain();
    let mut rcc = rcc.freeze(
        RccConfig::hsi()
            .sysclk(64.MHz())
            .hclk(64.MHz())
            .pclk1(32.MHz())
            .pclk2(64.MHz()),
        &mut flash.acr,
    );

    let mut delay = cortex_m::delay::Delay::new(cp.SYST, rcc.clocks.sysclk().raw());

    // PC13 LED: boot blink x2
    let mut gpioc = dp.GPIOC.split(&mut rcc);
    let mut led = gpioc.pc13.into_push_pull_output_with_state(&mut gpioc.crh, PinState::High);
    for _ in 0..2 {
        led.set_low();
        delay.delay_ms(80u32);
        led.set_high();
        delay.delay_ms(80u32);
    }

    // GPIOA: PA0..PA3 = ULN2003
    let mut gpioa = dp.GPIOA.split(&mut rcc);
    let pa0 = gpioa.pa0.into_push_pull_output_with_state(&mut gpioa.crl, PinState::Low);
    let pa1 = gpioa.pa1.into_push_pull_output_with_state(&mut gpioa.crl, PinState::Low);
    let pa2 = gpioa.pa2.into_push_pull_output_with_state(&mut gpioa.crl, PinState::Low);
    let pa3 = gpioa.pa3.into_push_pull_output_with_state(&mut gpioa.crl, PinState::Low);

    // USART1: PA9(TX), PA10(RX)
    let tx_pin = gpioa.pa9.into_alternate_push_pull(&mut gpioa.crh);
    let rx_pin = gpioa.pa10;

    // GPIOB: TB6612
    let mut gpiob = dp.GPIOB.split(&mut rcc);
    let a_in1 = gpiob.pb14.into_push_pull_output_with_state(&mut gpiob.crh, PinState::Low);
    let a_in2 = gpiob.pb15.into_push_pull_output_with_state(&mut gpiob.crh, PinState::Low);
    let b_in1 = gpiob.pb13.into_push_pull_output_with_state(&mut gpiob.crh, PinState::Low);
    let b_in2 = gpiob.pb12.into_push_pull_output_with_state(&mut gpiob.crh, PinState::Low);

    let mut motors = Motors::new(pa0, pa1, pa2, pa3, a_in1, a_in2, b_in1, b_in2);

    let mut protocol = Protocol::new(dp.USART1, tx_pin, rx_pin, &mut rcc);

    info!("Ready.");

    loop {
        match protocol.next_command() {
            Ok(cmd) => {
                motors.apply(&cmd);
            }
            Err(()) => {
                motors.stop_all();
            }
        }
        led.set_low();
        delay.delay_ms(30u32);
        led.set_high();
    }
}

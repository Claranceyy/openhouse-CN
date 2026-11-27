/* Host stand-in for the nRF52840 registers doorman/dongle-fw/src/link.c touches, so hop_test.c can build the
 * real link.c on a PC. Plain memory: nothing here behaves like the radio. Only for tests that call link.c's
 * own functions directly (never link_init / link_poll, which wait on the radio and the timer). */
#pragma once
#include <stdint.h>

typedef struct {
  volatile uint32_t TASKS_TXEN, TASKS_RXEN, TASKS_DISABLE, TASKS_RSSISTART;
  volatile uint32_t EVENTS_READY, EVENTS_ADDRESS, EVENTS_DISABLED, EVENTS_RSSIEND;
  volatile uint32_t SHORTS, INTENCLR, CRCSTATUS, RXMATCH, PACKETPTR, FREQUENCY, TXPOWER, MODE, PCNF0, PCNF1;
  volatile uint32_t BASE0, BASE1, PREFIX0, PREFIX1, TXADDRESS, RXADDRESSES, CRCCNF, CRCPOLY, CRCINIT;
  volatile uint32_t RSSISAMPLE, STATE, MODECNF0, POWER;
} NRF_RADIO_Type;
typedef struct {
  volatile uint32_t TASKS_START, TASKS_STOP, TASKS_CLEAR, TASKS_CAPTURE[6], MODE, BITMODE, PRESCALER, CC[6];
} NRF_TIMER_Type;
typedef struct { volatile uint32_t EVENTS_HFCLKSTARTED, TASKS_HFCLKSTART; } NRF_CLOCK_Type;
typedef struct { volatile uint32_t DEVICEID[2]; } NRF_FICR_Type;
typedef struct { volatile uint32_t READY, CONFIG, ERASEPAGE; } NRF_NVMC_Type;

extern NRF_RADIO_Type stub_radio;
extern NRF_TIMER_Type stub_timer1;
extern NRF_CLOCK_Type stub_clock;
extern NRF_FICR_Type stub_ficr;
extern NRF_NVMC_Type stub_nvmc;
#define NRF_RADIO  (&stub_radio)
#define NRF_TIMER1 (&stub_timer1)
#define NRF_CLOCK  (&stub_clock)
#define NRF_FICR   (&stub_ficr)
#define NRF_NVMC   (&stub_nvmc)

#define RADIO_SHORTS_READY_START_Msk       (1u << 0)
#define RADIO_SHORTS_END_DISABLE_Msk       (1u << 1)
#define RADIO_SHORTS_DISABLED_TXEN_Msk     (1u << 2)
#define RADIO_SHORTS_DISABLED_RXEN_Msk     (1u << 3)
#define RADIO_SHORTS_ADDRESS_RSSISTART_Msk (1u << 4)
#define RADIO_MODE_MODE_Nrf_2Mbit          1
#define RADIO_MODECNF0_RU_Fast             1
#define RADIO_MODECNF0_RU_Pos              0
#define RADIO_TXPOWER_TXPOWER_Pos8dBm      8
#define RADIO_TXPOWER_TXPOWER_Neg4dBm      0xFC
#define RADIO_PCNF0_LFLEN_Pos              0
#define RADIO_PCNF0_PLEN_Pos               24
#define RADIO_PCNF0_PLEN_8bit              0
#define RADIO_PCNF1_MAXLEN_Pos             0
#define RADIO_PCNF1_BALEN_Pos              16
#define RADIO_PCNF1_ENDIAN_Pos             24
#define RADIO_PCNF1_ENDIAN_Big             1
#define RADIO_CRCCNF_LEN_Two               2

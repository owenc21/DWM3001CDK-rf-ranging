
/**
 * Firmware module for building a distributed distance matrix among N nodes
 * Nodes will be uniquely identified by an ID in {0,..., N-1}
 * 
 * Based on firmware modules ss_twr_initiator.c and ss_twr_responder.c
 * 
 * @author Owen Capell
 */

#include "deca_probe_interface.h"
#include <config_options.h>
#include <deca_device_api.h>
#include <deca_spi.h>
#include <example_selection.h>
#include <port.h>
#include <shared_defines.h>
#include <shared_functions.h>
#include <stdio.h>

/* Example application name */
#define APP_NAME "SS TWR DIST CONN MAT"

/* Network configuration */
#define DEVICE_ID 0
#define NUM_DEVICES 4
#define SET_INIT_DEV (DEVICE_ID + 1) % NUM_DEVICES

/* Connectivity components */
static double connectivity_list[NUM_DEVICES];
static double connectivity_matrix[NUM_DEVICES][NUM_DEVICES];

/* Message definitions */

#define TYPE_ITITIATOR 0  // Message type indicating it's the receving node's turn to be an initiator 
#define TYPE_RANGING 1  // Message type indicating the sending node wants a response from the sender (for ranging) 

/**
 * Modified polling and response messages. See either ss_twr_initiator.c or ss_twr_responder.c for details
 */
static uint8_t poll_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 0, 0, 0, 0, 0xE0, 0, 0 };
static uint8_t resp_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 0, 0, 0, 0, 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/**
 * @struct message_header
 * @brief Contains metadata related to a transmitted message
 * 
 * Struct stores message metadata:
 *  Type of message
 *  Source device (ID of sender)
 *  Destination device (ID of intended recipient)
 */
typedef struct message_header{
    uint8_t type;
    uint8_t src;
    uint8_t dest;
} message_header;

/**
 * @struct message_payload
 * @brief Contains the payload (data) for a transmitted message
 * 
 * For ease of implementation, contains all possible data sent, even though at most one field
 * will be used for each transmission
 * In the future, it would be ideal if we weren't sending such large packets since so much information
 * is being unused.
 */
typedef struct message_payload{
    uint8_t poll_msg[12];
    uint8_t resp_msg[20];
    double connectivity_matrix[NUM_DEVICES][NUM_DEVICES];
} message_payload;

/**
 * @struct messsage
 * @brief A struct representing a message to be trasnmitted, containing header and payload
 * 
 */
typedef struct message{
    message_header header;
    message_payload payload;
} message;

/* Configuration Steps - See either ss_twr_initiator.c or ss_twr_responder.c for more details */

/* Default communication configuration. We use default non-STS DW mode. */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_128,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (129 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, /* STS disabled */
    DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* Inter-ranging delay period, in milliseconds. */
#define RNG_DELAY_MS 1000

/* Default antenna delay values for 64 MHz PRF. */
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385


/* Length of the common part of the message (up to and including the function code). */
#define ALL_MSG_COMMON_LEN 10
/* Indexes to access some of the fields in the frames defined above. */
#define ALL_MSG_SN_IDX          2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN         4

/* Frame sequence number, incremented after each transmission. */
static uint8_t frame_seq_nb = 0;

/* Buffer to store received response message.
 * Its size is adjusted to longest frame that this example code is supposed to handle. */
#define RX_BUF_LEN 160
static uint8_t rx_buffer[RX_BUF_LEN];

/* Hold copy of status register state here for reference so that it can be examined at a debug breakpoint. */
static uint32_t status_reg = 0;

/* Delay between frames, in UWB microseconds. */
#define POLL_TX_TO_RESP_RX_DLY_UUS 240
/* Receive response timeout. */
#define RESP_RX_TIMEOUT_UUS 400

/* Hold copies of computed time of flight and distance here for reference so that it can be examined at a debug breakpoint. */
static double tof;
static double distance;

/* Values for the PG_DELAY and TX_POWER registers reflect the bandwidth and power of the spectrum at the current
 * temperature. These values can be calibrated prior to taking reference measurements. */
extern dwt_txconfig_t txconfig_options;


/**
 * @fn print_matrix
 * Utility function to print the connectivity matrix
 */
void print_matrix(){
    for(int i=0; i<NUM_DEVICES; i++){
        for(int j=0; j<NUM_DEVICES; j++){
            printf("%3.3f M      ", connectivity_matrix[i][j]);
        }
        printf("\n");
    }
}


/**
 * @fn update_matrix
 * Copies the connectivity list into the appropriate entry in the
 * connectivity matrix
 */
void update_matrix(){
    memcpy(&connectivity_matrix[DEVICE_ID], &connectivity_list[0], NUM_DEVICES * sizeof(double));
}


/**
 * @fn initiator
 * Sets device to initiator, builds the connectivity list and updates the connectivity list
 */
void initiator(){

}


/**
 * @fn dist_matrix
 * Application entry point
 */
int dist_matrix(void){
    /* Start-up configuration, copied from ss_twr_initiator.c */
    printf("%s\n", APP_NAME);

    /* Configure SPI rate, DW3000 supports up to 36 MHz */
    port_set_dw_ic_spi_fastrate();

    /* Reset and initialize DW chip. */
    reset_DWIC(); /* Target specific drive of RSTn line into DW3000 low for a period. */

    Sleep(2); // Time needed for DW3000 to start up (transition from INIT_RC to IDLE_RC, or could wait for SPIRDY event)

    /* Probe for the correct device driver. */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    while (!dwt_checkidlerc()) /* Need to make sure DW IC is in IDLE_RC before proceeding */ { };
    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        printf("INIT FAILED\n");
        while (1) { };
    }

    /* Enabling LEDs here for debug so that for each TX the D1 LED will flash on DW3000 red eval-shield boards. */
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    /* Configure DW IC. See NOTE 13 below. */
    /* if the dwt_configure returns DWT_ERROR either the PLL or RX calibration has failed the host should reset the device */
    if (dwt_configure(&config))
    {
        printf("CONFIG FAILED\n");
        while (1) { };
    }

    /* Configure the TX spectrum parameters (power, PG delay and PG count) */
    dwt_configuretxrf(&txconfig_options);

    /* Apply default antenna delay value. See NOTE 2 below. */
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);


    // Need initial device to be set to initiator manually, otherwise rest are receiever and await being set to initiator
    if device == 0:
        do initiator
    else:
        do responder

    while(1):
        do responder
}
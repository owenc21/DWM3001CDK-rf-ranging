
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
#define DEVICE_ID 1
#define NUM_DEVICES 2
#define SET_INIT_DEV (DEVICE_ID + 1) % NUM_DEVICES

/* Connectivity components */
static double connectivity_list[NUM_DEVICES];
static double connectivity_matrix[NUM_DEVICES][NUM_DEVICES];

/* Message definitions */

#define TYPE_ITITIATOR 0  // Message type indicating it's the receving node's turn to be an initiator 
#define TYPE_RANGING 1  // Message type indicating the sending node wants a response from the sender (for ranging) 
#define TYPE_RESPONSE 2 // Message type indicating the sending node is a responder responding to a ranging request

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
typedef struct  message_payload{
    uint8_t poll_msg[12];
    uint8_t resp_msg[20];
    double connectivity_matrix[NUM_DEVICES][NUM_DEVICES];
    uint8_t padding[4]; // padding..?
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

/* Hold copy of status register state here for reference so that it can be examined at a debug breakpoint. */
static uint32_t status_reg = 0;

/* Delay between frames, in UWB microseconds. */
#define POLL_TX_TO_RESP_RX_DLY_UUS 240
/* Receive response timeout. */
#define RESP_RX_TIMEOUT_UUS 400

/* Delay between frames, in UWB microseconds. */
#define POLL_RX_TO_RESP_TX_DLY_UUS 650


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
 * Utility function that copies the connectivity list into the appropriate entry
 * in the connectivity matrix
 */
void update_matrix(){
    memcpy(&connectivity_matrix[DEVICE_ID], &connectivity_list[0], NUM_DEVICES * sizeof(double));
}


/**
 * @fn initiator
 * Sets device to initiator, builds the connectivity list and updates the connectivity list
 * Finishes by sending connectivity matrix along with initiatior start message to next device
 */
void initiator(){
    /* Configure the TX spectrum parameters (power, PG delay and PG count) */
    dwt_configuretxrf(&txconfig_options);

    /* Apply default antenna delay value. See NOTE 2 below. */
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);

    /* Set expected response's delay and timeout. See NOTE 1 and 5 below.
     * As this example only handles one incoming frame with always the same delay and timeout, those values can be set here once for all. */
    dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
    dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);

    /* Next can enable TX/RX states output on GPIOs 5 and 6 to help debug, and also TX/RX LEDs
     * Note, in real low power applications the LEDs should not be used. */
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

    // Start by printing out connectivity matrix (this will have been received unless this is first iter of device 0)
    print_matrix();

    // Initialize the message
    message_header header;
    header.type = TYPE_RANGING;
    header.src = DEVICE_ID;

    message_payload payload;
    
    message tx;
    tx.header = header;
    tx.payload = payload;

    uint8_t cur_device = 0;
    while(cur_device < NUM_DEVICES)
    {
        /* Skip ourselves */
        if(cur_device == DEVICE_ID){
            cur_device++;
            continue;
        }

        /* Update destination to cur_device. */
        tx.header.dest = cur_device;

        /* Write frame data to DW IC and prepare transmission  */
        tx.payload.poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
        dwt_writetxdata(sizeof(tx), (uint8_t*) &tx, 0);
        dwt_writetxfctrl(sizeof(tx), 0, 1);

        /* Start transmission, indicating that a response is expected so that reception is enabled automatically after the frame is sent and the delay
         * set by dwt_setrxaftertxdelay() has elapsed. */
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

        /* We assume that the transmission is achieved correctly, poll for reception of a frame or error/timeout. */
        waitforsysstatus(&status_reg, NULL, (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR), 0);

        /* Increment frame sequence number after transmission of the poll message (modulo 256). */
        frame_seq_nb++;

        if (status_reg & DWT_INT_RXFCG_BIT_MASK)
        {
            uint16_t frame_len;
            /* Clear good RX frame event in the DW IC status register. */
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

            /* A frame has been received, read it into a response message. */
            frame_len = dwt_getframelength();
            if (frame_len <= sizeof(message))
            {
                message response;
                dwt_readrxdata((uint8_t*) &response, frame_len, 0);

                /* Check that the response was a polling response and intended for us */
                if (response.header.dest == DEVICE_ID && response.header.type == TYPE_RESPONSE)
                {
                    uint32_t poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts;
                    int32_t rtd_init, rtd_resp;
                    float clockOffsetRatio;

                    /* Retrieve poll transmission and response reception timestamps */
                    poll_tx_ts = dwt_readtxtimestamplo32();
                    resp_rx_ts = dwt_readrxtimestamplo32();

                    /* Read carrier integrator value and calculate clock offset ratio. See NOTE 11 below. */
                    clockOffsetRatio = ((float)dwt_readclockoffset()) / (uint32_t)(1 << 26);

                    /* Get timestamps embedded in response message. */
                    resp_msg_get_ts(&response.payload.resp_msg[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
                    resp_msg_get_ts(&response.payload.resp_msg[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);

                    /* Compute time of flight and distance, using clock offset ratio to correct for differing local and remote clock rates */
                    rtd_init = resp_rx_ts - poll_tx_ts;
                    rtd_resp = resp_tx_ts - poll_rx_ts;

                    tof = ((rtd_init - rtd_resp * (1 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
                    distance = tof * SPEED_OF_LIGHT;
                    /* Display computed distance on LCD. */
                    printf("DIST: %3.2f m", distance);

                    /* Update connectivity list */
                    connectivity_list[cur_device] = distance;

                    /* We can break and move onto next device */
                    cur_device++;
                }
            }

        }
        else
        {
            /* Clear RX error/timeout events in the DW IC status register. */
            dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
        }

        /* Execute a delay between ranging exchanges. */
        Sleep(RNG_DELAY_MS);
    }

    /* We now have a fresh connectivity list, so update the matrix */
    update_matrix();

    /* Copy connectivity matrix to message and update dest to next initiator */
    tx.header.dest = SET_INIT_DEV;
    tx.header.type = TYPE_ITITIATOR;
    for(int i=0; i<NUM_DEVICES; i++){
        for(int j=0; j<NUM_DEVICES; j++){
            memcpy(&tx.payload.connectivity_matrix[i][j], &connectivity_matrix[i][j], sizeof(double));
        }
    }
    /* Write frame data to DW IC and prepare transmission  */
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(tx), (uint8_t*) &tx, 0);
    dwt_writetxfctrl(sizeof(tx), 0, 1);

    printf("DEBUG: sizeof(tx)=%d\n", sizeof(tx));

    /* Start transmission, indicating that a response is expected so that reception is enabled automatically after the frame is sent and the delay
        * set by dwt_setrxaftertxdelay() has elapsed. */
    dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

    return;
}


/**
 * @fn responder
 * Waits for any messages sent to specific device
 * If a polling message, responds appropriately
 * If an initiation message, moves into initiation 
 */
void responder(){
    message tx;
    tx.header.type = TYPE_RESPONSE;
    tx.header.src = DEVICE_ID;

    /* Configure the TX spectrum parameters (power, PG delay and PG count) */
    dwt_configuretxrf(&txconfig_options);

    /* Apply default antenna delay value. See NOTE 2 below. */
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);

    /* Next can enable TX/RX states output on GPIOs 5 and 6 to help debug, and also TX/RX LEDs
     * Note, in real low power applications the LEDs should not be used. */
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

    while (1)
    {
        /* Activate reception immediately. */
        dwt_rxenable(DWT_START_RX_IMMEDIATE);

        /* Poll for reception of a frame or error/timeout. */
        waitforsysstatus(&status_reg, NULL, (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR), 0);

        if (status_reg & DWT_INT_RXFCG_BIT_MASK)
        {
            uint16_t frame_len;

            /* Clear good RX frame event in the DW IC status register. */
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

            /* A frame has been received, read it into the local message response */
            frame_len = dwt_getframelength();
            printf("DEBUG: frame_len: %d\n", frame_len);
            if (frame_len <= sizeof(message))
            {
                message response;
                dwt_readrxdata((uint8_t*) &response, frame_len, 0);

                if (response.header.dest == DEVICE_ID && response.header.type == TYPE_RANGING)
                {
                    uint32_t resp_tx_time;
                    uint64_t poll_rx_ts, resp_tx_ts;
                    int ret;

                    /* Retrieve poll reception timestamp. */
                    poll_rx_ts = get_rx_timestamp_u64();

                    /* Compute response message transmission time. See NOTE 7 below. */
                    resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
                    dwt_setdelayedtrxtime(resp_tx_time);

                    /* Response TX timestamp is the transmission time we programmed plus the antenna delay. */
                    resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

                    /* Write all timestamps in the final message. See NOTE 8 below. */
                    resp_msg_set_ts(&tx.payload.resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
                    resp_msg_set_ts(&tx.payload.resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);

                    /* Write and send the response message. */
                    tx.payload.resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
                    tx.header.dest = response.header.src;
                    dwt_writetxdata(sizeof(tx), (uint8_t*) &tx, 0); /* Zero offset in TX buffer. */
                    dwt_writetxfctrl(sizeof(tx), 0, 1);          /* Zero offset in TX buffer, ranging. */
                    ret = dwt_starttx(DWT_START_TX_DELAYED);

                    /* If dwt_starttx() returns an error, abandon this ranging exchange and proceed to the next one. See NOTE 10 below. */
                    if (ret == DWT_SUCCESS)
                    {
                        /* Poll DW IC until TX frame sent event set. See NOTE 6 below. */
                        waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                        /* Clear TXFRS event. */
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                        /* Increment frame sequence number after transmission of the poll message (modulo 256). */
                        frame_seq_nb++;
                    }
                }
                else if(response.header.dest == DEVICE_ID && response.header.type == TYPE_ITITIATOR){
                    /* Copy distance matrix then become initiator */
                    for(int i=0; i<NUM_DEVICES; i++){
                        for(int j=0; j<NUM_DEVICES; j++){
                            memcpy(&connectivity_matrix[i][j], &response.payload.connectivity_matrix[i][j], sizeof(double));
                        }
                    }

                    printf("DEBUG: response matrix\n");
                    for(int i=0; i<NUM_DEVICES; i++){
                        for(int j=0; j<NUM_DEVICES; j++){
                            printf("%3.3f M      ", response.payload.connectivity_matrix[i][j]);
                        }
                        printf("\n");
                    }
                    initiator();
                }
            }
        }
        else
        {
            /* Clear RX error events in the DW IC status register. */
            dwt_writesysstatuslo(SYS_STATUS_ALL_RX_ERR);
        }
    }
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



    // Need initial device to be set to initiator manually, otherwise rest are receiever and await being set to initiator
    if(DEVICE_ID == 0)
    {
        initiator();
    }

    responder();

    // we should never get here
}
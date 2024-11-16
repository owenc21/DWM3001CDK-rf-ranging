
/**
 * Firmware module for building a distributed distance matrix among N nodes
 * Nodes will be uniquely identified by an ID in {0,}
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
#define APP_NAME "SS TWR N-DEV INIT"

/* Network configuration */
#define DEVICE_ID 'I'
#define NUM_DEVICES 3


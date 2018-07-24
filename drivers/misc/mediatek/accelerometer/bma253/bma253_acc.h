/* BMA253 motion sensor driver
 *
 *
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html

 * (C) Copyright 2011 Bosch Sensortec GmbH
 * All Rights Reserved
 */

#ifndef BMA253_H
#define BMA253_H
	 
#include <linux/ioctl.h>

/* 7-bit addr: 0x10 (SDO connected to GND); 0x11 (SDO connected to VDDIO) */
#define BMA253_I2C_ADDR				0x18
/* chip ID */
#define BMA253_FIXED_DEVID			0xFA

 /* BMA253 Register Map  (Please refer to BMA253 Specifications) */
#define BMA253_REG_DEVID			0x00
#define BMA253_REG_OFSX				0x16
#define BMA253_REG_OFSX_HIGH		0x1A
#define BMA253_REG_BW_RATE			0x10
#define BMA253_BW_MASK				0x1f
#define BMA253_BW_200HZ				0x0d
#define BMA253_BW_100HZ				0x0c
#define BMA253_BW_50HZ				0x0b
#define BMA253_BW_25HZ				0x0a
#define BMA253_REG_POWER_CTL		0x11		
#define BMA253_REG_DATA_FORMAT		0x0f
#define BMA253_RANGE_MASK			0x0f
#define BMA253_RANGE_2G				0x03
#define BMA253_RANGE_4G				0x05
#define BMA253_RANGE_8G				0x08
#define BMA253_REG_DATAXLOW			0x02
#define BMA253_REG_DATA_RESOLUTION	0x14
#define BMA253_MEASURE_MODE			0x80	
#define BMA253_SELF_TEST           	0x32
#define BMA253_SELF_TEST_AXIS_X		0x01
#define BMA253_SELF_TEST_AXIS_Y		0x02
#define BMA253_SELF_TEST_AXIS_Z		0x03
#define BMA253_SELF_TEST_POSITIVE	0x00
#define BMA253_SELF_TEST_NEGATIVE	0x04
#define BMA253_INT_REG_1           	0x16
#define BMA253_INT_REG_2          	0x17

#define BMA253_FIFO_MODE_REG                    0x3E
#define BMA253_FIFO_DATA_OUTPUT_REG             0x3F
#define BMA253_STATUS_FIFO_REG                  0x0E

#define BMA253_SUCCESS					0
#define BMA253_ERR_I2C					-1
#define BMA253_ERR_STATUS				-3
#define BMA253_ERR_SETUP_FAILURE		-4
#define BMA253_ERR_GETGSENSORDATA		-5
#define BMA253_ERR_IDENTIFICATION		-6
	 
#define BMA253_BUFSIZE					256

/* power mode */
#define BMA253_MODE_CTRL_REG			0x11

#define BMA253_MODE_CTRL__POS			5
#define BMA253_MODE_CTRL__LEN			3
#define BMA253_MODE_CTRL__MSK			0xE0
#define BMA253_MODE_CTRL__REG			BMA253_MODE_CTRL_REG

#define BMA253_LOW_POWER_CTRL_REG		0x12

#define BMA253_LOW_POWER_MODE__POS		6
#define BMA253_LOW_POWER_MODE__LEN		1
#define BMA253_LOW_POWER_MODE__MSK		0x40
#define BMA253_LOW_POWER_MODE__REG		BMA253_LOW_POWER_CTRL_REG

/* range */
#define BMA253_RANGE_SEL_REG			0x0F

#define BMA253_RANGE_SEL__POS			0
#define BMA253_RANGE_SEL__LEN			4
#define BMA253_RANGE_SEL__MSK			0x0F
#define BMA253_RANGE_SEL__REG			BMA253_RANGE_SEL_REG

/* bandwidth */
#define BMA253_BW_7_81HZ			0x08
#define BMA253_BW_15_63HZ			0x09
#define BMA253_BW_31_25HZ			0x0A
#define BMA253_BW_62_50HZ			0x0B
#define BMA253_BW_125HZ				0x0C
#define BMA253_BW_250HZ				0x0D
#define BMA253_BW_500HZ				0x0E
#define BMA253_BW_1000HZ			0x0F

#define BMA253_BW_SEL_REG			0x10

#define BMA253_BANDWIDTH__POS			0
#define BMA253_BANDWIDTH__LEN			5
#define BMA253_BANDWIDTH__MSK			0x1F
#define BMA253_BANDWIDTH__REG			BMA253_BW_SEL_REG

#endif

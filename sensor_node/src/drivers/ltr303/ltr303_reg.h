#ifndef LTR303_REG_H_
#define LTR303_REG_H_

#define LTR303_I2C_ADDR             0x29

#define LTR303_REG_CONTR            0x80
#define LTR303_REG_MEAS_RATE        0x85
#define LTR303_REG_PART_ID          0x86

#define LTR303_REG_DATA_CH1_0       0x88
#define LTR303_REG_DATA_CH1_1       0x89
#define LTR303_REG_DATA_CH0_0       0x8A
#define LTR303_REG_DATA_CH0_1       0x8B

#define LTR303_MODE_ACTIVE          0x01
#define LTR303_MODE_STANDBY         0x00

#endif /* LTR303_REG_H_ */
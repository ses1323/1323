#ifndef I2C_CTRL_H_
#define I2C_CTRL_H_

#include "scan_rf.h"

#if 0
void i2c_start(void);
void i2c_end(void);
int i2c_senddata(unsigned char id, unsigned char subaddr,unsigned char value);
int i2c_readdata(unsigned char id, unsigned char addr, unsigned char *value);
#endif

typedef struct i2c_adapter {
    int fd;
	unsigned char addr;
    unsigned char reg;
    unsigned char buf[2];
} i2c_ad_t;

typedef struct bq40_condition {
    int fd;
	unsigned int fullcharge_capacity;
    unsigned int temperature;
    unsigned int cycle_count;
    unsigned int state_of_health;
}bq40;

int dac_init(void);
int i2c_dev_init(void);


int bq40_ctl(void);
int  bq40_read_relative_charge(unsigned char *buf);
void *check_dev_thread(void *args);

#endif /* I2C_CTRL_H_ */


#include <stdio.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include "i2c_ctrl.h"

#define DAC40431_I2C_ADDR       (0x90 >> 1)
#define LIS2DE12_I2C_ADDR       (0x32 >> 1)
#define BQ40Z50_I2C_ADDR        (0x16 >> 1)

//DAC
#define STATUS_REG              0xD0
#define DGENERAL_CONFIG_REG     0xD1
#define MED_ALARM_CONFIG_REG    0xD2
#define TRIGGER_REG             0xD3
#define DAC_DATA_REG            0x21
#define DAC_MARGIN_HIGH_REG     0x25
#define DAC_MARGIN_LOW_REG      0x26

#define PMBUS_OP_REG            0x01
#define PMBUS_STATUS_BYTE_REG   0x78
#define PMBUS_VERSIONREG        0x98

//#define DEV_ADDR	            DAC40431_I2C_ADDR
#define REG_TEMP                0x0
#define REG_CON                 0x1
#define REG_TOS                 0x2
#define REG_THYS                0x3

#define MAX_CONV_MS             150
#define SIGN_MASK               (0x1 << 15)
#define TEMP_SHIFT              7
#define TEMP_MASK               (0xFF << TEMP_SHIFT)

#define WHO_AM_I_REG            0xf

#if BQ40Z50_I2C_ADDR
#define RELATIVE_STATE_OF_CHARGE 0xd
#define FULL_CHARGE_CAPACITY     0x10
#define STATE_OF_HEALTH          0x4f
#define TEMPERATURE              0x08
#define CYCLE_COUNT              0x17

#endif

unsigned int dac_data_bit = 8;//DAC43401 =8, DAC53401=10
i2c_ad_t dac_adapter;
bq40 g_bq40_status;
int i2c0_dev,i2c1_dev, bq40_dev,dac_dev=0;
extern led_t green_led, yellow_led;
extern unsigned char g_dev_charge[2];
extern float g_dev_temp;
extern float g_dev_core_val;

int i2c_read(int fd, unsigned char addr, unsigned char reg,
                unsigned char *val, unsigned char readc)
{
    struct i2c_rdwr_ioctl_data data;
    struct i2c_msg message[2];

    message[0].addr = addr;
    message[0].flags = 0;//write flags
    message[0].len = sizeof(reg);
    message[0].buf = &reg;

    message[1].addr = addr;
    message[1].flags = I2C_M_RD;//read flags
    message[1].len = readc; //sizeof(val);
    message[1].buf = val;

    data.msgs = message;
    data.nmsgs = 2;
    if(ioctl(fd,  I2C_RDWR, &data) < 0)
    {
        perror("read ioctl error\n");
        DEBUG_ERR("read ioctl error: %s (errno :%d)",strerror(errno),errno);
        return -1;
    }
    printf("buf[0] = %x,buf[1] = %x\n",data.msgs[1].buf[0],data.msgs[1].buf[1]);
    return 0;
}

int i2c_write(int fd, unsigned char dev_add, unsigned char reg_addr,
                unsigned short val, unsigned char writec)
{
    struct i2c_rdwr_ioctl_data data;
    struct i2c_msg message;
    unsigned char buf[10];

    buf[0] = reg_addr;
    for (int i = 0; i < writec; i++){
        if(i<9)
            buf[i+1] = ((val >> 8 * (i-1)) & 0xff);
    }
    buf[1] = (val>>8);
    buf[2] = (val & 0xff);

    message.addr = dev_add;
    message.flags = 0;//write flags
    message.len = writec+1;
    message.buf = buf;

    data.msgs = &message;
    data.nmsgs = 1;
    if(ioctl(fd,  I2C_RDWR, (unsigned long)&data) < 0)
    {
        perror("write ioctl error\n");
        DEBUG_ERR("write ioctl error: %s (errno :%d)",strerror(errno),errno);
        return -1;
    }
    usleep(1000);
    return 0;
}

int  bq40_read_relative_charge(unsigned char *buf)
{
    int bq40_fd;

    int ret = 0;
    unsigned char tmp[2];

    bq40_fd = bq40_dev;
    if(bq40_fd <= 0)
    {
        printf("can't open bq40_dev\n");
        return -1;
    }

    ret = i2c_read(bq40_fd, BQ40Z50_I2C_ADDR, RELATIVE_STATE_OF_CHARGE, buf, 2);
    return ret;
}

int  bq40_read_fullcharge_capacity(unsigned char *buf)  //FullChargeCapacity Ԥ�������ʱ�ĵ������   
{
    int bq40_fd;

    int ret = 0;
    unsigned char tmp[2]; 

    bq40_fd = bq40_dev;
    if(bq40_fd <= 0)
    {
        printf("can't open bq40_dev  in RemainingCapacity \n");
        return -1;
    }
    
    ret = i2c_read(bq40_fd, BQ40Z50_I2C_ADDR, FULL_CHARGE_CAPACITY, buf, 2);  //
    return ret;
}       

int  bq40_read_temperature(unsigned char *buf)  //Temperature   0.1 k
{
    int bq40_fd;

    int ret = 0;
    unsigned char tmp[2]; 

    bq40_fd = bq40_dev;
    if(bq40_fd <= 0)
    {
        printf("can't open bq40_dev  in Temperature \n");
        return -1;
    }
    
    ret = i2c_read(bq40_fd, BQ40Z50_I2C_ADDR, TEMPERATURE, buf, 2);  //
    return ret;
}
 
int  bq40_read_cycle_count(unsigned char *buf)  //CycleCount   ���ص�ؾ����ķŵ�ѭ��������Ĭ��ֵ�洢������ʱ���µ���������ֵCycle Count��  
{
    int bq40_fd;

    int ret = 0;
    unsigned char tmp[2]; 

    bq40_fd = bq40_dev;
    if(bq40_fd <= 0)
    {
        printf("can't open bq40_dev  in CycleCount \n");
        return -1;
    }
    
    ret = i2c_read(bq40_fd, BQ40Z50_I2C_ADDR, CYCLE_COUNT, buf, 2);  //
    return ret;
}  

int  bq40_read_state_of_health(unsigned char *buf)  //bq40_read_design_capacity 
{
    int bq40_fd;

    int ret = 0;
    unsigned char tmp[2]; 

    bq40_fd = bq40_dev;
    if(bq40_fd <= 0)
    {
        printf("can't open bq40_dev  in CycleCount \n");
        return -1;
    }
    
    ret = i2c_read(bq40_fd, BQ40Z50_I2C_ADDR, STATE_OF_HEALTH, buf, 2);  //
    return ret;
}  

void *check_dev_thread(void *args)
{
    unsigned char tmpbuf[2] = {0};
    static unsigned char last_battery = 0;
    DEBUG_INFO("check_dev_thread start");

	if( bq40_dev > 0 ){
		bq40_read_state_of_health(tmpbuf);
        g_bq40_status.state_of_health = tmpbuf[0];
        
        bq40_read_cycle_count(tmpbuf);
        g_bq40_status.cycle_count = (tmpbuf[1]<<8)+tmpbuf[0];
	}
	
    while(1)
    {
        if(bq40_dev > 0){
            bq40_read_relative_charge(g_dev_charge);

            if(g_dev_charge[0] != last_battery){
                last_battery = g_dev_charge[0];
                if(last_battery < 0x14){ //20%
                    DEBUG_INFO("check_dev_thread,change led,last_battery=%d",last_battery);
                    led_bright_ctl(green_led.bright_fd,0);
                    led_bright_ctl(yellow_led.bright_fd,1);
                }
            }
        }
        g_dev_temp = get_xadc_val(0);
        g_dev_core_val = get_xadc_val(2);

        bq40_read_temperature(tmpbuf);
        g_bq40_status.temperature = ((tmpbuf[1]<<8) + tmpbuf[0]) - 2731; 

        sleep(10);
    }
    DEBUG_INFO("quit check dev thread");
}

int  bq40_is_alive(void)
{
    int bq40_fd;

    int ret = 0;
    unsigned char tmp[2];

    bq40_dev = i2c0_dev;

    ret = i2c_read(bq40_dev, BQ40Z50_I2C_ADDR, RELATIVE_STATE_OF_CHARGE, tmp, 2);
    if(ret < 0){
        bq40_dev = 0;
        DEBUG_ERR("@@@bq40dev can not find");
    }
    return ret;
}

int  bq40_ctl(void)
{
    int bq40_fd;

    int value;
    unsigned char tmp[2];
    DEBUG_INFO("bq40_ctl\n");
    bq40_fd = i2c0_dev;
    if(bq40_fd < 0)
    {
        perror("can't open i2c-0\n");
        return 0;
    }
    int timeout = 3;

    while(timeout--){
        i2c_read(bq40_fd, BQ40Z50_I2C_ADDR, RELATIVE_STATE_OF_CHARGE, tmp, 2);
        sleep(1);
    }

    close(bq40_fd);

    return 0;

}

int  lis2d_ctl(void)
{
    int lis2d_fd;

    int value;
    unsigned char tmp[2];
    DEBUG_INFO("lis2d_ctl\n");
    lis2d_fd = i2c1_dev;//open("/dev/i2c-1", O_RDWR);
    if(lis2d_fd < 0)
    {
        DEBUG_ERR("can't open i2c-1\n");
        return 0;
    }
    int timeout = 3;

    while(timeout--){
        i2c_read(lis2d_fd, LIS2DE12_I2C_ADDR, WHO_AM_I_REG, tmp, 1);
        sleep(1);
    }

    close(lis2d_fd);

    return 0;

}

#define SCALE_TIMES   24
unsigned char scal_gain = 2;

unsigned short cal_dac_data(unsigned short thv_val)
{
    unsigned short dac_data = 0, data=0, vout = thv_val, shift_bit = 4 ;
    unsigned char gain = 2;
    float v_ref = 1.21;

    if(dac_data_bit == 8)
        shift_bit = 4;
    else
        shift_bit = 2;

    if(vout <= 58 ) { scal_gain = 2; }
    else if(vout <= 88) { scal_gain = 3; }
    else {
        printf(" vout out of range,vout = %d\n ", vout );
        return 0;
    }

    /* Vout= DAC_DATA/2^n *    Vref * Gain */
    if(gain != 0)
        data = ((vout * 1.0/SCALE_TIMES) / (v_ref * scal_gain *1.0)) * (1 << dac_data_bit );
    if(data <= 0xff )
        dac_data = data << shift_bit;
    else
        printf(" calculate  dac  error,data = 0x%x\n ", data );

    DEBUG_INFO("vout = %dV,data = %x, dac_data = %x, \n", vout,data, dac_data);

    return dac_data;

}

int dac_config(unsigned short *voltage)
{
    unsigned short val = 0x0, vol_val = *voltage ;
    if(dac_adapter.fd == 0)
        dac_init();
    if(dac_dev == 0)
        return -1;
    val = cal_dac_data(vol_val); //val= 0x0f00;
    i2c_write(dac_adapter.fd, dac_adapter.addr, DAC_DATA_REG, val, 2);
    //i2c_read(dac_adapter.fd, dac_adapter.addr, DAC_DATA_REG, dac_adapter.buf, 2);

    if(scal_gain == 2)
        val= 0x11e5;
    else if(scal_gain == 3)
        val= 0x11e6;
    else if(scal_gain == 4)
        val= 0x11e7;
    i2c_write(dac_adapter.fd, dac_adapter.addr, DGENERAL_CONFIG_REG, val, 2);

    val= 0x0010;
    i2c_write(dac_adapter.fd, dac_adapter.addr, TRIGGER_REG, val, 2);

}

int dac_init(void)
{
    int dac_fd = 0,value,ret = 0;
    unsigned short val = 0x0;
    unsigned char tmp[2];

    dac_adapter.fd =  i2c0_dev;
    dac_adapter.addr = DAC40431_I2C_ADDR;
    if(dac_adapter.fd == 0)
        return -1;

    dac_fd = dac_adapter.fd;
    ret = i2c_read(dac_fd, dac_adapter.addr, STATUS_REG, tmp, 2);
    if(ret == -1){
        dac_dev = 0;
        DEBUG_ERR("dac_dev can not find");
        return -1;
    } else {
        dac_dev = 1;
    }
    if( tmp[0] == 0x0C )
        dac_data_bit = 10;
    else if ( tmp[0] == 0x14 )
        dac_data_bit = 8;

#if 0
    val= 0x0800;//  voltage = 1.21v
    i2c_write(dac_adapter.fd, dac_adapter.addr, DAC_DATA_REG, val, 2);
    i2c_read(dac_fd, dac_adapter.addr, DAC_DATA_REG, tmp, 2);
#endif
    val= 0x11e5;
    ret |= i2c_write(dac_fd, dac_adapter.addr, DGENERAL_CONFIG_REG, val, 2);
    ret |= i2c_read(dac_fd, dac_adapter.addr, DGENERAL_CONFIG_REG, tmp, 2);

    val= 0x0010;
    ret |= i2c_write(dac_fd, dac_adapter.addr, TRIGGER_REG, val, 2);
    ret |= i2c_read(dac_fd, dac_adapter.addr, TRIGGER_REG, tmp, 2);

    //close(dac_adapter.fd);
    return ret;
}

int dac_exit(void)
{
    close(dac_adapter.fd);
}

int i2c_dev_exit(void)
{
    close(i2c0_dev);
    close(i2c1_dev);
}

int i2c_dev_init(void)
{
    i2c0_dev = open("/dev/i2c-0", O_RDWR);
    if(i2c0_dev<0)
    {
        DEBUG_ERR("can't open i2c-0");
        return -1;
    }

    i2c1_dev = open("/dev/i2c-1", O_RDWR);
    if(i2c1_dev<0)
    {
        DEBUG_ERR("can't open i2c-0");
        return -1;
    }

    bq40_is_alive();
    return 0;
}




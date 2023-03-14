#include<stdio.h>

#define VCCAUX_VOL "/sys/bus/iio/devices/iio:device0/in_voltage2_vccint_raw"

#define PS_TEMP "/sys/bus/iio/devices/iio:device0/in_temp0_ps_temp_raw"
#define PL_TEMP "/sys/bus/iio/devices/iio:device0/in_temp2_pl_temp_raw"


float conversion_temp(int adc_val)
{
    float tem;
    tem = ((float)adc_val * 509.314)/65536.0-280.23;
    return tem;
}

float conversion_voltage(int adc_val)
{
    float c_vol;
    c_vol = ((float)adc_val * 3.0)/65536.0;
    return c_vol;
}

float get_xadc_val (int adc_type)
{
    int temp;
    float conv_tem=0.0;
    FILE *fp = NULL;

    if(adc_type == 0) {
        fp = fopen(PS_TEMP, "r");
    } else if (adc_type == 1){
        fp = fopen(PL_TEMP, "r");
    }else if (adc_type == 2){
        fp = fopen(VCCAUX_VOL, "r");
    } else {
        perror("Can not find this temp type\n");
    }

    if(fp == NULL)
	{
		printf("open failed!\n");
        return 0;
	}

    fscanf(fp,"%d",&temp);
    if(adc_type == 0 || adc_type == 1)
        conv_tem = conversion_temp(temp);
    else if(adc_type == 2)
        conv_tem = conversion_voltage(temp);

    fclose(fp);

    return conv_tem;
}


int temperature_init(void)
{
	int ps_temp,pl_temp;
	FILE *fp;
	fp = fopen(PS_TEMP, "r");
	if(fp == NULL)
	{
		printf("open %s failed!\n", PS_TEMP);
        return -1;
	}
	fscanf(fp,"%d",&ps_temp);
	printf("PS temperature : %f\n",conversion_temp(ps_temp));
    fclose(fp);

	fp = fopen(PL_TEMP, "r");
	if(fp == NULL)
	{
		printf("open %s failed!\n", PL_TEMP);
        return -1;
	}

	fscanf(fp,"%d",&pl_temp);
    printf("PL temperature : %f\n",conversion_temp(pl_temp));
    //printf("PL temperature : %f\n",((float)pl_temp)*509.314/65536.0-280.23);
    fclose(fp);

	return 0;
}


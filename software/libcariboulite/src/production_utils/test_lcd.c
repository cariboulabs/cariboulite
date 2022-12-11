#include <stdio.h>
#include "lcd.h"

void callback(void* context, int key1, int key2)
{
	static int k1 = 0;
	static int k2 = 0;
	lcd_st* dev = (lcd_st*)context;
	printf("Pressed %d, %d\n", key1, key2);
	
	if (key1 != k1 || key2 != k2) 
	{
		char buf1[10];
		char buf2[10];
		sprintf(buf1, "Key1: %d", key1);
		sprintf(buf2, "Key2: %d", key2);
		lcd_writeln(dev, buf1, buf2, 1);
		k1 = key1;
		k2 = key2;
	}
}

int main ()
{
	lcd_st dev = {0};
	lcd_init(&dev, callback, &dev);
	lcd_clear_screan(&dev);
	
	lcd_write(&dev, 0, 0, "Hello");
	lcd_write(&dev, 1, 0, "World");

	
	lcd_close(&dev);
	return 0;
}
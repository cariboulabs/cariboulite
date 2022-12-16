#ifndef __LCD_H__
#define __LCD_H__

#ifdef __cplusplus
extern "C" {
#endif


#include <zmq.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>

typedef void (*lcd_key_callback)(void* context, int key1, int key2);
typedef enum
{
	lcd_button_bottom = 0,
	lcd_button_top = 1,
} lcd_button_en;

typedef struct
{
	// thread
	pthread_t comm_thread;
	bool thread_running;
	void* cb_context;
	lcd_key_callback cb;
	pid_t py_pid;
	
	// key state
	int key1;
	int key2;
	
	// lcd
	void *context;
    void *requester;
} lcd_st; 

int lcd_init(lcd_st* dev, lcd_key_callback cb, void* cb_context);
int lcd_close(lcd_st* dev);
int lcd_clear_screan(lcd_st* dev);
int lcd_write(lcd_st* dev, int row, int col, char* text);
int lcd_writeln(lcd_st* dev, char* line1, char* line2, int clear);
int lcd_get_keys(lcd_st* dev, int* key1, int* key2);
int lcd_set_params(lcd_st* dev, int brightness, int contrast);


#ifdef __cplusplus
}
#endif

#endif //__LCD_H__
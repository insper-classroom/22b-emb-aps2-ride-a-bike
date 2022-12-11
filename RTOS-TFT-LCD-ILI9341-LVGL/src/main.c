/************************************************************************/
/* includes                                                             */
/************************************************************************/

#include <asf.h>
#include <string.h>
#include "ili9341.h"
#include "lvgl.h"
#include "touch/touch.h"

#include "screen1.h"
#include "screen2.h"

#include "arm_math.h"

LV_FONT_DECLARE(dseg10);
LV_FONT_DECLARE(dseg20);
LV_FONT_DECLARE(dseg30);
LV_FONT_DECLARE(dseg50);
LV_FONT_DECLARE(dseg70);

#define SENSOR PIOA
#define SENSOR_ID ID_PIOA
#define SENSOR_IDX 19
#define SENSOR_IDX_MASK (1u << SENSOR_IDX)

/************************************************************************/
/* LCD / LVGL                                                           */
/************************************************************************/

#define LV_HOR_RES_MAX          (240)
#define LV_VER_RES_MAX          (320)

/*A static or global variable to store the buffers*/
static lv_disp_draw_buf_t disp_buf;

/*Static or global buffer(s). The second buffer is optional*/
static lv_color_t buf_1[LV_HOR_RES_MAX * LV_VER_RES_MAX];
static lv_disp_drv_t disp_drv;          /*A variable to hold the drivers. Must be static or global.*/
static lv_indev_drv_t indev_drv;

/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_LCD_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY            (tskIDLE_PRIORITY)

#define TASK_TIME_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_TIME_STACK_PRIORITY            (tskIDLE_PRIORITY)

#define TASK_PLAY_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_PLAY_STACK_PRIORITY            (tskIDLE_PRIORITY)

SemaphoreHandle_t xSemaphoreTIME;
SemaphoreHandle_t xSemaphoreTIME2;
SemaphoreHandle_t xSemaphorePLAY;
SemaphoreHandle_t xSemaphoreReset;
SemaphoreHandle_t xSemaphoreSensor;

lv_obj_t * labelTIME;
lv_obj_t * labelTIME2;
lv_obj_t * HORAS;
lv_obj_t * MINUTOS;
lv_obj_t * VELOCIDADEINSTANTANEA1;
lv_obj_t * VELOCIDADEINSTANTANEA2;
lv_obj_t * VELOCIDADEMEDIA1;
lv_obj_t * VELOCIDADEMEDIA2;
lv_obj_t * DISTANCIA1;
lv_obj_t * DISTANCIA2;
lv_obj_t * reset;
lv_obj_t * labelReset;
lv_obj_t * play;
lv_obj_t * labelPlay;

static lv_style_t style;

static lv_obj_t * tela1;
static lv_obj_t * tela2;

volatile char PLAYPAUSE = 0;

typedef struct  {
  uint32_t year;
  uint32_t month;
  uint32_t day;
  uint32_t week;
  uint32_t hour;
  uint32_t minute;
  uint32_t second;
} calendar;

typedef struct  {
  uint32_t hora;
  uint32_t minuto;
  uint32_t segundo;
} tempo;

tempo tempoAtual;

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);
void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type);
void TC_init(Tc * TC, int ID_TC, int TC_CHANNEL, int freq);
static void RTT_init(float freqPrescale, uint32_t IrqNPulses, uint32_t rttIRQSource);

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}

/************************************************************************/
/* Funções                                                              */
/************************************************************************/

void atualiza_velocidade_Instantanea(float velocidadeInstantanea) {
	char str1[10], str2[10];
	if(velocidadeInstantanea<10){
		sprintf(str1, "    %d.", (uint32_t)velocidadeInstantanea);
	}else{
		sprintf(str1, "%02d.", (uint32_t)velocidadeInstantanea);
	}
	
	sprintf(str2, "%d", ((uint32_t)(velocidadeInstantanea * 10)) % 10);
	lv_label_set_text(VELOCIDADEINSTANTANEA1, str1);
	lv_label_set_text(VELOCIDADEINSTANTANEA2, str2);
}

void atualiza_velocidade_Media(float velocidadeMedia) {
	char str1[10], str2[10];
	if(velocidadeMedia<10){
		sprintf(str1, "    %d.", (uint32_t)velocidadeMedia);
	}else{
		sprintf(str1, "%02d.", (uint32_t)velocidadeMedia);
	}
	
	sprintf(str2, "%d", ((uint32_t)(velocidadeMedia * 10)) % 10);
	
	lv_label_set_text(VELOCIDADEMEDIA1, str1);
	lv_label_set_text(VELOCIDADEMEDIA2, str2);
}

void atualiza_distancia(float distancia) {
	char str1[10], str2[10];
	if(distancia<10){
		sprintf(str1, "    %d.", (uint32_t)distancia);
	}else{
		sprintf(str1, "%02d.", (uint32_t)distancia);
	}
	
	sprintf(str2, "%d", ((uint32_t)(distancia * 10)) % 10);
	
	lv_label_set_text(DISTANCIA1, str1);
	lv_label_set_text(DISTANCIA2, str2);
}

void RTT_Handler(void) {
	uint32_t ul_status;
	ul_status = rtt_get_status(RTT);

	/* IRQ due to Alarm */
	if ((ul_status & RTT_SR_ALMS) == RTT_SR_ALMS) {
		
	}
}

void sensor_callback(void){
	xSemaphoreGiveFromISR(xSemaphoreSensor, 0);
}

/************************************************************************/
/* lvgl                                                                 */
/************************************************************************/

void TC1_Handler(void) {
	/**
	* Devemos indicar ao TC que a interrupção foi satisfeita.
	* Isso é realizado pela leitura do status do periférico
	**/
	volatile uint32_t status = tc_get_status(TC0, 1);

	/** Muda o estado do LED (pisca) **/
	xSemaphoreGiveFromISR(xSemaphoreTIME2, 0);
}

void RTC_Handler(void) {	
	uint32_t ul_status = rtc_get_status(RTC);
	
    /* seccond tick */
    if ((ul_status & RTC_SR_SEC) == RTC_SR_SEC) {	
	// o código para irq de segundo vem aqui
		xSemaphoreGiveFromISR(xSemaphoreTIME, 0);
	}
	
    /* Time or date alarm */
    if ((ul_status & RTC_SR_ALARM) == RTC_SR_ALARM) {
    	// o código para irq de alame vem aqui
    }

    rtc_clear_status(RTC, RTC_SCCR_SECCLR);
    rtc_clear_status(RTC, RTC_SCCR_ALRCLR);
    rtc_clear_status(RTC, RTC_SCCR_ACKCLR);
    rtc_clear_status(RTC, RTC_SCCR_TIMCLR);
    rtc_clear_status(RTC, RTC_SCCR_CALCLR);
    rtc_clear_status(RTC, RTC_SCCR_TDERRCLR);
}

static void event_config(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		LV_LOG_USER("Clicked");
		printf("Config 1 Clicked\n");
		lv_scr_load(tela2);
	}
}

static void event_config2(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		LV_LOG_USER("Clicked");
		printf("Config 2 Clicked\n");
		lv_scr_load(tela1);
	}
	
}

static void event_reset(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		LV_LOG_USER("Clicked");
		xSemaphoreGiveFromISR(xSemaphoreReset, 0);
		printf("Reset Clicked\n");
	}
	
}

static void event_play(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		LV_LOG_USER("Clicked");
		printf("Play Clicked\n");
		xSemaphoreGiveFromISR(xSemaphorePLAY, 0);
	}
	
}

void primeira_tela(void) {
	/***********************
	/    Tela principal    /
	***********************/

	// Tela 1
	lv_obj_t * tela = lv_img_create(tela1);
	lv_img_set_src(tela, &screen1);
	lv_obj_align(tela, LV_ALIGN_CENTER, 0, 0);

	/***********************
	/  Estilos dos botões  /
	***********************/
	
	// Estilo do botão
	style;
    lv_style_init(&style);
	lv_style_set_bg_color(&style, lv_color_white());
	lv_style_set_text_color(&style, lv_color_black());
    lv_style_set_border_width(&style, 0);

	/************************
	/ Botao de Configuracao /
	************************/
	
	// Botão de configuração
	lv_obj_t * config = lv_btn_create(tela1);
	lv_obj_add_event_cb(config, event_config, LV_EVENT_ALL, NULL);
	lv_obj_align(config, LV_ALIGN_TOP_RIGHT, -7, 3);
	lv_obj_add_style(config, &style, 0);
	
	// Label do botão de configuração
	lv_obj_t * labelConfig = lv_label_create(config);
	lv_label_set_text(labelConfig, LV_SYMBOL_SETTINGS);
	lv_obj_center(labelConfig);

	/***********************
	/ DURAÇÃO DO PERCURSO /
	**********************/

	// HORAS
	HORAS = lv_label_create(tela1);
	lv_obj_align(HORAS, LV_ALIGN_TOP_MID, -32, 66);
	lv_obj_set_style_text_font(HORAS, &dseg20, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(HORAS, lv_color_black(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(HORAS, "%02d", 0);

	// MINUTOS
	MINUTOS = lv_label_create(tela1);
	lv_obj_align(MINUTOS, LV_ALIGN_TOP_MID, 46, 66);
	lv_obj_set_style_text_font(MINUTOS, &dseg20, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(MINUTOS, lv_color_black(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(MINUTOS, "%02d", 0);

	/*************************
	/ VELOCIDADE INSTANTANEA /
	*************************/

	// VELOCIDADE INSTANTANEA 1
	VELOCIDADEINSTANTANEA1 = lv_label_create(tela1);
	lv_obj_align(VELOCIDADEINSTANTANEA1, LV_ALIGN_CENTER, -40, 17);
	lv_obj_set_style_text_font(VELOCIDADEINSTANTANEA1, &dseg50, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(VELOCIDADEINSTANTANEA1, lv_color_black(), LV_STATE_DEFAULT);
	lv_label_set_text(VELOCIDADEINSTANTANEA1, "    0.");
	
	// VELOCIDADE INSTANTANEA 2
	VELOCIDADEINSTANTANEA2 = lv_label_create(tela1);
	lv_obj_align(VELOCIDADEINSTANTANEA2, LV_ALIGN_CENTER, 15, 26);
	lv_obj_set_style_text_font(VELOCIDADEINSTANTANEA2, &dseg30, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(VELOCIDADEINSTANTANEA2, lv_color_black(), LV_STATE_DEFAULT);
	lv_label_set_text(VELOCIDADEINSTANTANEA2, "0");

	/*************************
	/    VELOCIDADE MEDIA    /
	*************************/

	// VELOCIDADE MEDIA 1
	VELOCIDADEMEDIA1 = lv_label_create(tela1);
	lv_obj_align(VELOCIDADEMEDIA1, LV_ALIGN_CENTER, -90, 93);
	lv_obj_set_style_text_font(VELOCIDADEMEDIA1, &dseg30, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(VELOCIDADEMEDIA1, lv_color_black(), LV_STATE_DEFAULT);
	lv_label_set_text(VELOCIDADEMEDIA1, "    0.");

	// VELOCIDADE MEDIA 2
	VELOCIDADEMEDIA2 = lv_label_create(tela1);
	lv_obj_align(VELOCIDADEMEDIA2, LV_ALIGN_CENTER, -57, 99);
	lv_obj_set_style_text_font(VELOCIDADEMEDIA2, &dseg20, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(VELOCIDADEMEDIA2, lv_color_black(), LV_STATE_DEFAULT);
	lv_label_set_text(VELOCIDADEMEDIA2, "0");

	/*************************
	/       Distancia        /
	*************************/

	// Distancia 1
	DISTANCIA1 = lv_label_create(tela1);
	lv_obj_align(DISTANCIA1, LV_ALIGN_CENTER, 40, 93);
	lv_obj_set_style_text_font(DISTANCIA1, &dseg30, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(DISTANCIA1, lv_color_black(), LV_STATE_DEFAULT);
	lv_label_set_text(DISTANCIA1, "    0.");

	// Distancia 2
	DISTANCIA2 = lv_label_create(tela1);
	lv_obj_align(DISTANCIA2, LV_ALIGN_CENTER, 73, 99);
	lv_obj_set_style_text_font(DISTANCIA2, &dseg20, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(DISTANCIA2, lv_color_black(), LV_STATE_DEFAULT);
	lv_label_set_text(DISTANCIA2, "0");

	/*************************
	/   Reseta a Corrida     /
	*************************/

	// Botão de reset
	reset = lv_btn_create(tela1);
	lv_obj_add_event_cb(reset, event_reset, LV_EVENT_ALL, NULL);
	lv_obj_align(reset, LV_ALIGN_BOTTOM_MID, -50, -12);
	lv_obj_add_style(reset, &style, 0);
	
	// Label do botão de configuração
	labelReset = lv_label_create(reset);
	lv_label_set_text(labelReset, LV_SYMBOL_REFRESH);
	lv_obj_center(labelReset);

	/*************************
	/      Play e Pause      /
	*************************/

	// Botão de Play e Pause
	play = lv_btn_create(tela1);
	lv_obj_add_event_cb(play, event_play, LV_EVENT_ALL, NULL);
	lv_obj_align(play, LV_ALIGN_BOTTOM_MID, 50, -12);
	lv_obj_add_style(play, &style, 0);
	
	// Label do botão de Play e Pause
	labelPlay = lv_label_create(play);
	lv_label_set_text(labelPlay, LV_SYMBOL_PAUSE);
	lv_obj_center(labelPlay);
			
	/*************************
	/       Tempo Real       /
	*************************/

	// Label Time
	labelTIME = lv_label_create(tela1);
	lv_obj_align(labelTIME, LV_ALIGN_BOTTOM_MID, -98, 0);
	lv_obj_set_style_text_font(labelTIME, &dseg10, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelTIME, lv_color_black(), LV_STATE_DEFAULT);
}

void segunda_tela(void) {
	lv_obj_t * tel = lv_img_create(tela2);
	lv_img_set_src(tel, &screen2);
	lv_obj_align(tel, LV_ALIGN_CENTER, 0, 0);
	
	static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_white());
    lv_style_set_text_color(&style, lv_color_black());
    lv_style_set_border_width(&style, 0);

	lv_obj_t * bnt = lv_btn_create(tela2);
	lv_obj_add_event_cb(bnt, event_config2, LV_EVENT_ALL, NULL);
	lv_obj_align(bnt, LV_ALIGN_TOP_RIGHT, -7, 3);
	lv_obj_add_style(bnt, &style, 0);
	
	lv_obj_t * labelbnt = lv_label_create(bnt);
	lv_label_set_text(labelbnt, LV_SYMBOL_SETTINGS);
	lv_obj_center(labelbnt);

	// Label Time
	labelTIME2 = lv_label_create(tela2);
	lv_obj_align(labelTIME2, LV_ALIGN_BOTTOM_MID, -98, 0);
	lv_obj_set_style_text_font(labelTIME2, &dseg10, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelTIME2, lv_color_black(), LV_STATE_DEFAULT);	
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_lcd(void *pvParameters) {
	int px, py;
	
	calendar rtc_initial = {2022, 3, 19, 12, 15, 45 ,1};

	/* Leitura do valor atual do RTC */           
    uint32_t current_hour, current_min, current_sec;
    uint32_t current_year, current_month, current_day, current_week;
    rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
    rtc_get_date(RTC, &current_year, &current_month, &current_day, &current_week);

	
	tela1 = lv_obj_create(NULL);
	lv_scr_load(tela1);

	tela2 = lv_obj_create(NULL);
	//lv_scr_load(tela2);

	primeira_tela();
	segunda_tela();
	lv_obj_clear_flag(tela1, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(tela2, LV_OBJ_FLAG_SCROLLABLE);
	
    RTC_init(RTC, ID_RTC, rtc_initial, RTC_IER_SECEN);
	TC_init(TC0, ID_TC1, 1, 1);
	
	for (;;)  {

		lv_tick_inc(50);
		lv_task_handler();
		vTaskDelay(50);
	}
}

void task_time(void) {
	tempoAtual.hora = 0;
	tempoAtual.minuto = 0;
	tempoAtual.segundo = 0;
	RTT_init(100, 0, NULL);
	uint32_t pulsos_rtt = 0; 
	float dt = 0;
	float w = 0;
	float v = 0;
	float r =  0.508/2;
	float f = 0;
	float d = 0;
	float v_media = 0;
	float t_total = 0;
	while (1) {
		if( xSemaphoreTake(xSemaphoreSensor, 0) ){
			pulsos_rtt = rtt_read_timer_value(RTT);
			RTT_init(100, 0, NULL);
			dt = pulsos_rtt * 0.01;
			t_total+=dt;
			d+=2*PI*r;
			v_media = d*3.6/t_total;
			atualiza_velocidade_Media(v_media);
			atualiza_distancia(d/1000);
			f =1/dt;
			w = 2*PI*f;
			v = w*r*3.6;
			atualiza_velocidade_Instantanea(v);
			printf("Velocidade média: %.1f\n", v_media);
			printf("Distancia: %.1f\n", d);
			printf("Velocidade instantanea: %.1f\n", v);
		}

		
		if( xSemaphoreTake(xSemaphoreTIME, 0) ){
			uint32_t current_hour, current_min, current_sec;
			uint32_t current_year, current_month, current_day, current_week;
			rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
			rtc_get_date(RTC, &current_year, &current_month, &current_day, &current_week);
			
			// lv_label_set_text_fmt(labelMIN, "%02d", current_min);
			if (current_sec % 2 == 0) {
				lv_label_set_text_fmt(labelTIME, "%02d:%02d:%02d", current_hour, current_min, current_sec);
				lv_label_set_text_fmt(labelTIME2, "%02d:%02d:%02d", current_hour, current_min, current_sec);
			} else {
				lv_label_set_text_fmt(labelTIME, "%02d %02d %02d", current_hour, current_min, current_sec);
				lv_label_set_text_fmt(labelTIME2, "%02d %02d %02d", current_hour, current_min, current_sec);
			}
      	}

		if( xSemaphoreTake(xSemaphoreTIME2, 0) ){
			tempoAtual.segundo++;
			if (tempoAtual.segundo == 60) {
				tempoAtual.segundo = 0;
				tempoAtual.minuto++;
			}
			if (tempoAtual.minuto == 60) {
				tempoAtual.minuto = 0;
				tempoAtual.hora++;
			}
			if (tempoAtual.hora == 24) {
				tempoAtual.hora = 0;
			}
			lv_label_set_text_fmt(HORAS, "%02d", tempoAtual.hora);
			lv_label_set_text_fmt(MINUTOS, "%02d", tempoAtual.minuto);
			printf("HORA: %02d:%02d:%02d\n", tempoAtual.hora, tempoAtual.minuto, tempoAtual.segundo);
      	}
		if( xSemaphoreTake(xSemaphoreReset, 0) ){
			tempoAtual.hora = 0;
			tempoAtual.minuto = 0;
			tempoAtual.segundo = 0;
			v_media = 0;
			d = 0;
			t_total = 0;
			atualiza_velocidade_Media(0);
			atualiza_distancia(0);
			lv_label_set_text_fmt(HORAS, "%02d", tempoAtual.hora);
			lv_label_set_text_fmt(MINUTOS, "%02d", tempoAtual.minuto);
			printf("HORA: %02d:%02d:%02d\n", tempoAtual.hora, tempoAtual.minuto, tempoAtual.segundo);
		}
	}
	
}

void task_play(void) {	
	while (1) {
		if( xSemaphoreTake(xSemaphorePLAY, 0) ){
			if (PLAYPAUSE == 0) {
				PLAYPAUSE = 1;
				// lv_label_set_text(labelPlay, LV_SYMBOL_PLAY);
				tc_start(TC0, 1);
			} else {
				PLAYPAUSE = 0;
				labelPlay = lv_label_create(play);
				// lv_label_set_text(labelPlay, LV_SYMBOL_PAUSE);
				tc_stop(TC0, 1);
			}
      	}
	}
	
}

/************************************************************************/
/* configs                                                              */
/************************************************************************/

static void configure_lcd(void) {
	/**LCD pin configure on SPI*/
	pio_configure_pin(LCD_SPI_MISO_PIO, LCD_SPI_MISO_FLAGS);  //
	pio_configure_pin(LCD_SPI_MOSI_PIO, LCD_SPI_MOSI_FLAGS);
	pio_configure_pin(LCD_SPI_SPCK_PIO, LCD_SPI_SPCK_FLAGS);
	pio_configure_pin(LCD_SPI_NPCS_PIO, LCD_SPI_NPCS_FLAGS);
	pio_configure_pin(LCD_SPI_RESET_PIO, LCD_SPI_RESET_FLAGS);
	pio_configure_pin(LCD_SPI_CDS_PIO, LCD_SPI_CDS_FLAGS);
	
	ili9341_init();
	ili9341_backlight_on();
}

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = USART_SERIAL_EXAMPLE_BAUDRATE,
		.charlength = USART_SERIAL_CHAR_LENGTH,
		.paritytype = USART_SERIAL_PARITY,
		.stopbits = USART_SERIAL_STOP_BIT,
	};

	/* Configure console UART. */
	stdio_serial_init(CONSOLE_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	setbuf(stdout, NULL);
}

static void RTT_init(float freqPrescale, uint32_t IrqNPulses, uint32_t rttIRQSource) {

	uint16_t pllPreScale = (int) (((float) 32768) / freqPrescale);
	
	rtt_sel_source(RTT, false);
	rtt_init(RTT, pllPreScale);
	
	if (rttIRQSource & RTT_MR_ALMIEN) {
		uint32_t ul_previous_time;
		ul_previous_time = rtt_read_timer_value(RTT);
		while (ul_previous_time == rtt_read_timer_value(RTT));
		rtt_write_alarm_time(RTT, IrqNPulses+ul_previous_time);
	}

	/* config NVIC */
	NVIC_DisableIRQ(RTT_IRQn);
	NVIC_ClearPendingIRQ(RTT_IRQn);
	NVIC_SetPriority(RTT_IRQn, 4);
	NVIC_EnableIRQ(RTT_IRQn);

	/* Enable RTT interrupt */
	if (rttIRQSource & (RTT_MR_RTTINCIEN | RTT_MR_ALMIEN))
	rtt_enable_interrupt(RTT, rttIRQSource);
	else
	rtt_disable_interrupt(RTT, RTT_MR_RTTINCIEN | RTT_MR_ALMIEN);
	
}

void io_init(void){
	pmc_enable_periph_clk(SENSOR_ID);
	pio_configure(SENSOR, PIO_INPUT, SENSOR_IDX_MASK, NULL);
	pio_handler_set(SENSOR,
		SENSOR_ID,
	    SENSOR_IDX_MASK,
	    PIO_IT_FALL_EDGE,
	    sensor_callback);
	pio_enable_interrupt(SENSOR, SENSOR_IDX_MASK);
	pio_get_interrupt_status(SENSOR);
	NVIC_EnableIRQ(SENSOR_ID);
    NVIC_SetPriority(SENSOR_ID, 4);
}
/************************************************************************/
/* port lvgl                                                            */
/************************************************************************/

void my_flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p) {
	ili9341_set_top_left_limit(area->x1, area->y1);   ili9341_set_bottom_right_limit(area->x2, area->y2);
	ili9341_copy_pixels_to_screen(color_p,  (area->x2 + 1 - area->x1) * (area->y2 + 1 - area->y1));
	
	/* IMPORTANT!!!
	* Inform the graphics library that you are ready with the flushing*/
	lv_disp_flush_ready(disp_drv);
}

void my_input_read(lv_indev_drv_t * drv, lv_indev_data_t*data) {
	int px, py, pressed;
	
	if (readPoint(&px, &py))
		data->state = LV_INDEV_STATE_PRESSED;
	else
		data->state = LV_INDEV_STATE_RELEASED; 
	
	data->point.x = py;
	data->point.y = 320 - px;
}

void configure_lvgl(void) {
	lv_init();
	lv_disp_draw_buf_init(&disp_buf, buf_1, NULL, LV_HOR_RES_MAX * LV_VER_RES_MAX);
	
	lv_disp_drv_init(&disp_drv);            /*Basic initialization*/
	disp_drv.draw_buf = &disp_buf;          /*Set an initialized buffer*/
	disp_drv.flush_cb = my_flush_cb;        /*Set a flush callback to draw to the display*/
	disp_drv.hor_res = LV_HOR_RES_MAX;      /*Set the horizontal resolution in pixels*/
	disp_drv.ver_res = LV_VER_RES_MAX;      /*Set the vertical resolution in pixels*/

	lv_disp_t * disp;
	disp = lv_disp_drv_register(&disp_drv); /*Register the driver and save the created display objects*/
	
	/* Init input on LVGL */
	lv_indev_drv_init(&indev_drv);
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	indev_drv.read_cb = my_input_read;
	lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);
}

void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type) {
	/* Configura o PMC */
	pmc_enable_periph_clk(ID_RTC);

	/* Default RTC configuration, 24-hour mode */
	rtc_set_hour_mode(rtc, 0);

	/* Configura data e hora manualmente */
	rtc_set_date(rtc, t.year, t.month, t.day, t.week);
	rtc_set_time(rtc, t.hour, t.minute, t.second);

	/* Configure RTC interrupts */
	NVIC_DisableIRQ(id_rtc);
	NVIC_ClearPendingIRQ(id_rtc);
	NVIC_SetPriority(id_rtc, 4);
	NVIC_EnableIRQ(id_rtc);

	/* Ativa interrupcao via alarme */
	rtc_enable_interrupt(rtc,  irq_type);
}

void TC_init(Tc * TC, int ID_TC, int TC_CHANNEL, int freq){
	uint32_t ul_div;
	uint32_t ul_tcclks;
	uint32_t ul_sysclk = sysclk_get_cpu_hz();

	/* Configura o PMC */
	pmc_enable_periph_clk(ID_TC);

	/** Configura o TC para operar em  freq hz e interrupçcão no RC compare */
	tc_find_mck_divisor(freq, ul_sysclk, &ul_div, &ul_tcclks, ul_sysclk);
	tc_init(TC, TC_CHANNEL, ul_tcclks | TC_CMR_CPCTRG);
	tc_write_rc(TC, TC_CHANNEL, (ul_sysclk / ul_div) / freq);

	/* Configura NVIC*/
  	NVIC_SetPriority(ID_TC, 4);
	NVIC_EnableIRQ((IRQn_Type) ID_TC);
	tc_enable_interrupt(TC, TC_CHANNEL, TC_IER_CPCS);
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/
int main(void) {
	/* board and sys init */
	board_init();
	sysclk_init();
	configure_console();
	io_init();

	/* LCd, touch and lvgl init*/
	configure_lcd();
	ili9341_set_orientation(ILI9341_FLIP_Y | ILI9341_SWITCH_XY);
	configure_touch();
	configure_lvgl();

	xSemaphoreTIME = xSemaphoreCreateBinary();
	xSemaphoreTIME2 = xSemaphoreCreateBinary();
	xSemaphorePLAY = xSemaphoreCreateBinary();
	xSemaphoreReset = xSemaphoreCreateBinary();
	xSemaphoreSensor = xSemaphoreCreateBinary();

	/* Create task to control oled */
	if (xTaskCreate(task_lcd, "LCD", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create lcd task\r\n");
	}

	if (xTaskCreate(task_time, "TIME", TASK_TIME_STACK_SIZE, NULL, TASK_TIME_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create time task\r\n");
	}

	if (xTaskCreate(task_play, "PLAY", TASK_TIME_STACK_SIZE, NULL, TASK_PLAY_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create time task\r\n");
	}
	
	/* Start the scheduler. */
	vTaskStartScheduler();

	while(1){ }
}

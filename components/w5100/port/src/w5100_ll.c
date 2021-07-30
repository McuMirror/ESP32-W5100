
#include "sdkconfig.h"

#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "driver/spi_master.h"

#include "w5100_ll.h"

#define W_PCK( address, data ) ( __builtin_bswap32( ( 0xF0000000 | ( address ) << 8 | ( data ) ) ) )
#define R_PCK( address )	   ( __builtin_bswap32( ( 0x0F000000 | ( address ) << 8 ) ) )

spi_device_handle_t w5100_spi_handle = NULL;
SemaphoreHandle_t eth_mutex;

#if CONFIG_W5100_POLLING_SPI_TRANS
#define W5100_TR spi_device_polling_transmit
#else
#define W5100_TR spi_device_transmit
#endif

static void IRAM_ATTR w5100_SPI_EN_assert( spi_transaction_t *trans )
{
	GPIO.out_w1ts = ( 1 << CONFIG_W5100_SPI_EN_GPIO );
}

static void IRAM_ATTR w5100_SPI_En_deassert( spi_transaction_t *trans )
{
	GPIO.out_w1tc = ( 1 << CONFIG_W5100_SPI_EN_GPIO );
}

void w5100_ll_hw_reset( void )
{
	// /RESET is inverted in my board
	ESP_ERROR_CHECK( gpio_set_level( CONFIG_W5100_RESET_GPIO, 1 ) );
	vTaskDelay( 1 );
	ESP_ERROR_CHECK( gpio_set_level( CONFIG_W5100_RESET_GPIO, 0 ) );
}

void w5100_spi_init( void )
{
	ESP_ERROR_CHECK( gpio_config( &( const gpio_config_t ) {
		.pin_bit_mask = BIT64( CONFIG_W5100_RESET_GPIO ) | BIT64( CONFIG_W5100_SPI_EN_GPIO ),
		.mode = GPIO_MODE_OUTPUT } ) );
	ESP_ERROR_CHECK( !( eth_mutex = xSemaphoreCreateMutex() ) );
	ESP_ERROR_CHECK( pdFALSE == xSemaphoreTake( eth_mutex, portMAX_DELAY ) );

	ESP_ERROR_CHECK( spi_bus_add_device(
		CONFIG_W5100_SPI_BUS - 1,
		&( spi_device_interface_config_t ) {
			.clock_speed_hz = CONFIG_W5100_SPI_CLCK,
			.spics_io_num = CONFIG_W5100_CS,
			.queue_size = CONFIG_W5100_SPI_QUEUE_SIZE,
			.pre_cb = w5100_SPI_EN_assert,
			.post_cb = w5100_SPI_En_deassert },
		&w5100_spi_handle ) );
	ESP_ERROR_CHECK( spi_device_acquire_bus( w5100_spi_handle, portMAX_DELAY ) );
	ESP_ERROR_CHECK( pdFALSE == xSemaphoreGive( eth_mutex ) );
}

void w5100_spi_deinit( void )
{
	ESP_ERROR_CHECK( pdFALSE == xSemaphoreTake( eth_mutex, portMAX_DELAY ) );
	spi_device_release_bus( w5100_spi_handle );
	ESP_ERROR_CHECK( spi_bus_remove_device( w5100_spi_handle ) );
	ESP_ERROR_CHECK( pdFALSE == xSemaphoreGive( eth_mutex ) );
	vSemaphoreDelete( eth_mutex );
}

int w5100_read( const uint16_t addr, uint8_t *const data_rx, const uint32_t size )
{
	spi_transaction_t trans = { .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA, .length = 32 };
	ESP_ERROR_CHECK( pdFALSE == xSemaphoreTake( eth_mutex, portMAX_DELAY ) );
	for ( uint32_t i = 0; i < size; ++i )
	{
		*( uint32_t * )&trans.tx_buffer = R_PCK( addr + i );
		ESP_ERROR_CHECK( W5100_TR( w5100_spi_handle, &trans ) );
		data_rx[ i ] = trans.rx_data[ 3 ];
	}
	ESP_ERROR_CHECK( pdFALSE == xSemaphoreGive( eth_mutex ) );

	return 0;
}

int w5100_write( const uint16_t addr, const uint8_t *const data_tx, const uint32_t size )
{
	spi_transaction_t trans = { .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA, .length = 32 };
	ESP_ERROR_CHECK( pdFALSE == xSemaphoreTake( eth_mutex, portMAX_DELAY ) );
	for ( uint32_t i = 0; i < size; ++i )
	{
		*( uint32_t * )&trans.tx_buffer = W_PCK( addr + i, data_tx[ i ] );
		ESP_ERROR_CHECK( W5100_TR( w5100_spi_handle, &trans ) );
	}
	ESP_ERROR_CHECK( pdFALSE == xSemaphoreGive( eth_mutex ) );

	return 0;
}
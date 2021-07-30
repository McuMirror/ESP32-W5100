
#include "sdkconfig.h"

#include "esp_event.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "esp_sntp.h"

#include "eth_main.h"
#include "esp_http_client_example.h"
#include "mqtt_example.h"
#include "w5100_main.h"
#include "w5100_ll.h"

#define GOT_IPV4 BIT0

struct
{
	esp_event_handler_instance_t eth_evt_hdl;
	esp_event_handler_instance_t got_ip_evt_hdl;
} evt_hdls;

static const char *TAG = "w5100_main";
EventGroupHandle_t eth_ev;

/** Event handler for Ethernet events */
static void eth_event_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
	uint8_t mac_addr[ 6 ] = { 0 };
	/* we can get the ethernet driver handle from event data */
	esp_eth_handle_t eth_handle = *( esp_eth_handle_t * )event_data;

	switch ( event_id )
	{
		case ETHERNET_EVENT_CONNECTED:
			esp_eth_ioctl( eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr );
			ESP_LOGV( TAG, "Ethernet Link Up" );
			ESP_LOGV( TAG,
					  "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
					  mac_addr[ 0 ],
					  mac_addr[ 1 ],
					  mac_addr[ 2 ],
					  mac_addr[ 3 ],
					  mac_addr[ 4 ],
					  mac_addr[ 5 ] );
			break;
		case ETHERNET_EVENT_DISCONNECTED:
			ESP_LOGI( TAG, "Ethernet Link Down" );
			break;
		case ETHERNET_EVENT_START:
			ESP_LOGI( TAG, "Ethernet Started" );
			break;
		case ETHERNET_EVENT_STOP:
			ESP_LOGI( TAG, "Ethernet Stopped" );
			break;
		default:
			break;
	}
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
	ip_event_got_ip_t *event = ( ip_event_got_ip_t * )event_data;
	const esp_netif_ip_info_t *ip_info = &event->ip_info;

	ESP_LOGI( TAG, "Ethernet Got IP Address" );
	ESP_LOGI( TAG, "~~~~~~~~~~~" );
	ESP_LOGI( TAG, "ETHIP:" IPSTR, IP2STR( &ip_info->ip ) );
	ESP_LOGI( TAG, "ETHMASK:" IPSTR, IP2STR( &ip_info->netmask ) );
	ESP_LOGI( TAG, "ETHGW:" IPSTR, IP2STR( &ip_info->gw ) );
	ESP_LOGI( TAG, "~~~~~~~~~~~" );

	xEventGroupSetBits( eth_ev, GOT_IPV4 );
}

void init( void )
{
	eth_ev = xEventGroupCreate();

	// Register user defined event handers
	ESP_ERROR_CHECK(
		esp_event_handler_instance_register( ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL, &evt_hdls.eth_evt_hdl ) );
	ESP_ERROR_CHECK(
		esp_event_handler_instance_register( IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL, &evt_hdls.got_ip_evt_hdl ) );
}

#ifdef CONFIG_TEST_DEINIT
void deinit( void )
{
	ESP_LOGD( TAG, "Starting deinit" );
	eth_deinit();
	ESP_ERROR_CHECK( esp_event_handler_instance_unregister( IP_EVENT, IP_EVENT_ETH_GOT_IP, evt_hdls.got_ip_evt_hdl ) );
	ESP_ERROR_CHECK( esp_event_handler_instance_unregister( ETH_EVENT, ESP_EVENT_ANY_ID, evt_hdls.eth_evt_hdl ) );
	ESP_ERROR_CHECK( esp_event_loop_delete_default() );
	// ESP_ERROR_CHECK(esp_netif_deinit());
	ESP_ERROR_CHECK( spi_bus_free( SPI3_HOST ) );
	vEventGroupDelete( eth_ev );
	ESP_LOGD( TAG, "Deinit finished" );
}
#endif

void w5100_start()
{
	init();
	eth_init( &( struct eth_ifconfig ){
		.hostname = "w5100_esp32",
		.w5100_cfg =
		{
			.init = w5100_spi_init,
			.deinit = w5100_spi_deinit,
			.hw_reset = w5100_ll_hw_reset,
			.read = w5100_read,
			.write = w5100_write
		},
#ifdef CONFIG_TEST_STATIC_IP
		.sip =
		{
			.ip.u8 = { 192, 168, 251, 220 },
			.nm.u8 = { 255, 255, 255, 0 },
			.gw.u8 = { 192, 168, 251, 1 },
			.p_dns.u8 = { 1, 1, 1, 1 },
			.s_dns.u8 = { 8, 8, 8, 8 },
			.f_dns.u8 = { 8, 8, 4, 4 },
		},
#endif
	} );

	xEventGroupWaitBits( eth_ev, GOT_IPV4, pdFALSE, pdTRUE, portMAX_DELAY );
	setenv( "TZ", CONFIG_TZ_ENV, 1 );
	tzset();

	sntp_setoperatingmode( SNTP_OPMODE_POLL );
	sntp_setservername( 0, "pool.ntp.org" );
	sntp_init();

	int retry = 1;
	while ( sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET )
	{
		ESP_LOGI( TAG, "Waiting for system time to be set... (%d)", retry++ );
		vTaskDelay( 2000 / portTICK_PERIOD_MS );
	}

	http_client_test();
	mqtt_example();
#ifdef CONFIG_TEST_DEINIT
	vTaskDelay( pdMS_TO_TICKS( 60000 ) );
	deinit();
#endif
}
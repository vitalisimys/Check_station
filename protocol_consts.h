#ifndef PROTOCOL_CONSTS_H
#define PROTOCOL_CONSTS_H

#include <stdint.h>
#include <cstdint>

// ============================================================================
// СЕТЕВЫЕ НАСТРОЙКИ
// ============================================================================

// Значение по умолчанию. Актуальный IP станции выбирается в UI.
constexpr const char* STATION_IP          = "192.168.7.1";
constexpr const char* CONTROLLER_IP       = "192.168.7.23";
constexpr const char* TRAFFIC_MCAST_IP    = "224.0.1.3";

// Порты управления
constexpr uint16_t STATION_PORT           = 6267;
constexpr uint16_t CONTROLLER_PORT        = 6267;

// Порты трафика
constexpr uint16_t TRAFFIC_SRC_PORT       = 12160;
constexpr uint16_t TRAFFIC_DST_PORT       = 12182;

// Тайминги
constexpr int      HEARTBEAT_INTERVAL_MS  = 1000;
constexpr int      HEARTBEAT_TIMEOUT_MS   = 10000;
constexpr int      TRAFFIC_INTERVAL_MS    = 20;
/** Таймаут подтверждения вкл/выкл тракта (сек), как t_out в frame_ppm_status */
constexpr int      TRACT_POWER_ACK_TIMEOUT_SEC = 100;

// ============================================================================
// ПРОТОКОЛ УПРАВЛЕНИЯ
// ============================================================================

constexpr uint16_t MAIN_MARKER            = 0xABCD;
constexpr uint16_t ACK_MARKER             = 0xDCBA;
constexpr uint16_t HEADER_SIZE            = 10;

// Команды
constexpr uint16_t CMD_MOD_START          = 0x0F01;
constexpr uint16_t CMD_MOD_STARTACK       = 0x0F02;
constexpr uint16_t CMD_MOD_MODE           = 0x0F03;
constexpr uint16_t CMD_TRACT_CONTROL      = 0x0504;
constexpr uint16_t CMD_READ_ALL_INDIC     = 0x0510;
constexpr uint16_t CMD_SET_FREQRX         = 0x0601;
constexpr uint16_t CMD_SET_FREQTX         = 0x0602;

// Индикации
// Индикации подтверждения вкл/выкл тракта (как EUDP_IND_TRAKT_*_SE в пульте)
constexpr uint16_t IND_TRAKT_OFF_SE       = 0x8536;
constexpr uint16_t IND_TRAKT_ON_SE        = 0x8537;

constexpr uint16_t IND_FREQRX             = 0x8601;
constexpr uint16_t IND_FREQTX             = 0x8602;
constexpr uint16_t IND_RSSI               = 0x8609;
constexpr uint16_t IND_SNR                = 0x860B;
constexpr uint16_t IND_CHREADY            = 0x860D;
constexpr uint16_t IND_DIAGN_DEVICE       = 0x8629;

// Параметры
constexpr uint8_t  MODTYPE_DEFAULT        = 0x03;
constexpr uint8_t  DEFAULT_TRACT_NUM      = 3;
constexpr uint8_t  DEFAULT_TRACT_MODE     = 2;
constexpr uint8_t  DEFAULT_PULT_NUM       = 1;

// ============================================================================
// ПРОТОКОЛ ТРАФИКА (RTP/UDP)
// ============================================================================

constexpr uint16_t TRAFFIC_PACKET_SIZE    = 332;
constexpr uint16_t RTP_HEADER_SIZE        = 12;
constexpr uint16_t RTP_PAYLOAD_SIZE       = 320;
constexpr uint8_t  RTP_PAYLOAD_TYPE       = 91;
constexpr uint32_t RTP_SSRC               = 0x1A1A0BEC;

// DSCP
constexpr uint8_t  DSCP_DEFAULT           = 0x00;
constexpr uint8_t  DSCP_STREAMVOICE       = 0x1A;
constexpr uint8_t  ECN_DEFAULT            = 0x00;

// ============================================================================
// АНАЛИЗАТОР: спектр (Гц) — начальный sweep 220…470 МГц (= границы по умолчанию)
// ============================================================================

constexpr std::uint64_t ANALYZER_STREAM_START_HZ_DEFAULT = 220000000ULL;
constexpr std::uint64_t ANALYZER_STREAM_STOP_HZ_DEFAULT = 470000000ULL;

#endif // PROTOCOL_CONSTS_H

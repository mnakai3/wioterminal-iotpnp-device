#pragma once

#include <string>
#include <Adafruit_TinyUSB.h>

namespace ExtFlashLoader
{
	class QSPIFlash;
}

class Storage
{
private:
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

public:
	std::string WiFiSSID;
	std::string WiFiPassword;
	std::string IdScope;
	std::string RegistrationId;
	std::string SymmetricKey;

    Storage(ExtFlashLoader::QSPIFlash& flash);
	void Load();
	void Load_orig();
	void Save();
	void Save_orig();
	void Erase();

	Adafruit_USBD_MSC UsbMsc_;
	int read_lba;
	int read_size;
	int write_lba;
	int write_size;
	Adafruit_USBD_MSC::read_callback_t _read;
	Adafruit_USBD_MSC::write_callback_t _write;
	Adafruit_USBD_MSC::flush_callback_t _flush;

	void begin();
	void registerCallback(
		Adafruit_USBD_MSC::read_callback_t read,
		Adafruit_USBD_MSC::write_callback_t write,
		Adafruit_USBD_MSC::flush_callback_t flush) {
			_read = read;
			_write = write;
			_flush = flush;
	};

	int32_t MscReadCB(uint32_t lba, void* buffer, uint32_t bufsize);
	int32_t MscWriteCB(uint32_t lba, uint8_t* buffer, uint32_t bufsize);
	void MscFlushCB();

private:
	ExtFlashLoader::QSPIFlash& Flash_;

};

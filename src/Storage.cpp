#include <Arduino.h>
#include "Storage.h"

#include <MsgPack.h>
#include <ExtFlashLoader.h>

#include <ArduinoJson.h>
#define CONFIG_FILE "/settings.json"

#include <SFUD/Seeed_SFUD.h>
#define DISK_BLOCK_NUM  (SPIFLASH.flashSize() / DISK_BLOCK_SIZE)
#define DISK_BLOCK_SIZE SECTORSIZE

static auto FlashStartAddress = reinterpret_cast<const uint8_t* const>(0x04000000);

Storage::Storage(ExtFlashLoader::QSPIFlash& flash) :
	Flash_(flash)
{
	Flash_.initialize();    
	Flash_.reset();
	Flash_.enterToMemoryMode();

	read_lba = -1;
	read_size = 0;
	write_lba = -1;
	write_size = 0;
}

void Storage::begin()
{
	SPIFLASH.begin(50000000UL);
	UsbMsc_.setID("Seeed", "MSC", "0.1");
	UsbMsc_.setCapacity(DISK_BLOCK_NUM, DISK_BLOCK_SIZE);
	UsbMsc_.setReadWriteCallback(_read, _write, _flush);
	UsbMsc_.setUnitReady(true);
	UsbMsc_.begin();
}

void Storage::Load_orig()
{
	if (memcmp(&FlashStartAddress[0], "AZ01", 4) != 0)
	{
		WiFiSSID.clear();
		WiFiPassword.clear();
		IdScope.clear();
		RegistrationId.clear();
		SymmetricKey.clear();
	}
	else
	{
		MsgPack::Unpacker unpacker;
		unpacker.feed(&FlashStartAddress[8], *(const uint32_t*)&FlashStartAddress[4]);

		MsgPack::str_t str[5];
		unpacker.deserialize(str[0], str[1], str[2], str[3], str[4]);

		WiFiSSID = str[0].c_str();
		WiFiPassword = str[1].c_str();
		IdScope = str[2].c_str();
		RegistrationId = str[3].c_str();
		SymmetricKey = str[4].c_str();
	}
}

void Storage::Load()
{
	// Remount the FAT just in case
	SPIFLASH.end();
	SPIFLASH.begin(50000000UL);

	File file = SPIFLASH.open(CONFIG_FILE, "r");
	if (!file) {
		return;
	}

	StaticJsonDocument<1024> doc;
	DeserializationError error = deserializeJson(doc, file);
	if (error)
		Serial.println(F("Failed to read file, using default configuration"));

	WiFiSSID = doc["wifi"]["ssid"].as<std::string>();
	WiFiPassword = doc["wifi"]["password"].as<std::string>();
	IdScope = doc["azure"]["idscope"].as<std::string>();
	RegistrationId = doc["azure"]["regid"].as<std::string>();
	SymmetricKey = doc["azure"]["symkey"].as<std::string>();

	file.close();
}

void Storage::Save_orig()
{
    MsgPack::Packer packer;
	{
		MsgPack::str_t str[5];
		str[0] = WiFiSSID.c_str();
		str[1] = WiFiPassword.c_str();
		str[2] = IdScope.c_str();
		str[3] = RegistrationId.c_str();
		str[4] = SymmetricKey.c_str();
		packer.serialize(str[0], str[1], str[2], str[3], str[4]);
	}

	std::vector<uint8_t> buf(4 + 4 + packer.size());
	memcpy(&buf[0], "AZ01", 4);
	*(uint32_t*)&buf[4] = packer.size();
	memcpy(&buf[8], packer.data(), packer.size());

	ExtFlashLoader::writeExternalFlash(Flash_, 0, &buf[0], buf.size(), [](std::size_t bytes_processed, std::size_t bytes_total, bool verifying) { return true; });
}

void Storage::Save()
{
	File file = SPIFLASH.open(CONFIG_FILE, "w");
	if (!file) {
		return;
	}

	StaticJsonDocument<1024> doc;
	doc["wifi"]["ssid"] = WiFiSSID;
	doc["wifi"]["password"] = WiFiPassword;
	doc["azure"]["idscope"] = IdScope;
	doc["azure"]["regid"] = RegistrationId;
	doc["azure"]["symkey"] = SymmetricKey;

	serializeJsonPretty(doc, file);
	file.close();
}

void Storage::Erase()
{
	WiFiSSID.clear();
	WiFiPassword.clear();
	IdScope.clear();
	RegistrationId.clear();
	SymmetricKey.clear();

	Save();
}

int32_t Storage::MscReadCB(uint32_t lba, void* buffer, uint32_t bufsize)
{
	const sfud_flash *flash = sfud_get_device_table();
	int addr = DISK_BLOCK_SIZE * lba + (512 * 0x01f8);
	// 8blocks
	if (read_lba != lba) {
		read_lba = lba;
		read_size = bufsize;
	} else {
		addr += read_size;
		read_size += bufsize;
		if (read_size >= DISK_BLOCK_SIZE) read_size = 0;
	}

	if (sfud_read(flash, addr, bufsize, (uint8_t*)buffer) != SFUD_SUCCESS) {
		return -1;
	}

	return bufsize;
}

int32_t Storage::MscWriteCB(uint32_t lba, uint8_t* buffer, uint32_t bufsize)
{
	const sfud_flash *flash = sfud_get_device_table();
	int addr = DISK_BLOCK_SIZE * lba + (512 * 0x01f8);
	// 8blocks
	if (write_lba != lba) {
		write_lba = lba;
		write_size = bufsize;
	} else {
		addr += write_size;
		write_size += bufsize;
		if (write_size >= DISK_BLOCK_SIZE) write_size = 0;
	}

	if (flash->chip.write_mode & SFUD_WM_PAGE_256B) {
		if (write_size == bufsize) {
		// erase 4KB block
			if (sfud_erase(flash, addr, DISK_BLOCK_SIZE) != SFUD_SUCCESS) {
				return -1;
			}
		}
		// write 2 x 256bytes
		if (sfud_write(flash, addr, 256, buffer) != SFUD_SUCCESS) {
			return -1;
		}
		if (sfud_write(flash, addr + 256, 256, buffer + 256) != SFUD_SUCCESS) {
			return -1;
		}
	} else if (flash->chip.write_mode & SFUD_WM_AAI) {
		if (sfud_erase_write(flash, addr, bufsize, buffer) != SFUD_SUCCESS) {
			return -1;
		}
	}

	return bufsize;
}

void Storage::MscFlushCB(void)
{
	read_lba = -1;
	read_size = 0;
	write_lba = -1;
	write_size = 0;
}

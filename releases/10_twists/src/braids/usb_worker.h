//

#ifndef BRAIDS_USB_WORKER_H_
#define BRAIDS_USB_WORKER_H_

#include "braids/settings.h"
#include "hardware/flash.h"

#define OPT_MAGIC 0x60
#define OPT_SHAPE_1   1
#define OPT_SHAPE_2   2
#define OPT_SHAPE_3   3
#define OPT_SHAPE_4   4
#define OPT_SHAPE_5   5
#define OPT_SHAPE_6   6
#define OPT_ADVCA     7
#define OPT_ADATTACK  8
#define OPT_ADDECAY   9

#define CONFIG_LENGTH NUM_AVAILABLE_SHAPES+4
#define SYSEX_INDEX_MANUFACTURER 1
#define SYSEX_INDEX_COMMAND 2
#define SYSEX_INDEX_LENGTH 3
#define SYSEX_MANUFACTURER_DEV 125
#define SYSEX_COMMAND_PREVIEW 1
#define SYSEX_COMMAND_WRITE_FLASH 2
#define SYSEX_COMMAND_READ 3

namespace braids {
  
class UsbWorker {
 public:

  UsbWorker() { }
  ~UsbWorker() { }

  void Init();
  void Run();

 private:
  void MidiTask();
  void ProcessSysExCommand(uint8_t *packet);
  void SetConfigFromFlash();
  int SetConfigFromSysEx(uint8_t *packet);
  void PostConfigProcessing();

  uint8_t config_[CONFIG_LENGTH];
  uint8_t packet_[CONFIG_LENGTH+10];
  uint32_t configFlashAddr_ = (PICO_FLASH_SIZE_BYTES - 4096) - (PICO_FLASH_SIZE_BYTES - 4096)%4096;

  DISALLOW_COPY_AND_ASSIGN(UsbWorker);
};

}  // namespace braids

#endif // BRAIDS_USB_WORKER_H_

# Measurement-station hardware provisioning

The normal and XLR boards run the same application binary. The hardware type is
stored once in the existing `storage` NVS namespace under `is_xrl`. Existing
stations already carrying this key do not need to be provisioned again.

For a new or deliberately erased station, generate a 24 KiB NVS image with the
ESP-IDF NVS partition generator. Choose either `standard.csv` or `xlr.csv`:

```powershell
python "$env:IDF_PATH\components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py" generate standard.csv standard-nvs.bin 0x6000
```

Flash the generated image at the NVS offset without erasing the entire device:

```powershell
esptool.py --chip esp32s3 write_flash 0x9000 standard-nvs.bin
```

The application validates all stored values. If the keys are missing or
invalid, it defaults to a standard station with two required sensors and five
virtual XLR sensors. Flashing a provisioning image replaces the complete NVS
partition, so use it only for a new or intentionally reset station.

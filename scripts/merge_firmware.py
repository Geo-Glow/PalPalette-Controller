# scripts/merge_firmware.py
Import("env")
import os
import shutil


def after_build(source, target, env):
    try:
        build_dir = env.subst("$BUILD_DIR")
        project_dir = env.subst("$PROJECT_DIR")
        firmware_dir = os.path.join(project_dir, "firmware")
        os.makedirs(firmware_dir, exist_ok=True)

        board = env.get("BOARD")
        mcu = env.get("BOARD_MCU", "").lower()

        if "esp32" in mcu:
            bootloader = os.path.join(build_dir, "bootloader.bin")
            partitions = os.path.join(build_dir, "partitions.bin")
            app = os.path.join(build_dir, "firmware.bin")
            output = os.path.join(build_dir, "combined-firmware.bin")
            final_path = os.path.join(firmware_dir, "esp32_firmware.bin")

            if all(os.path.isfile(f) for f in [bootloader, partitions, app]):
                print(f"🔧 Merging ESP32 firmware for board {board}...")
                cmd = f"esptool --chip {mcu} merge_bin -o {output} --flash_mode dio --flash_freq 80m --flash_size 4MB 0x0 {bootloader} 0x8000 {partitions} 0x10000 {app}"
                env.Execute(cmd)
                shutil.copy(output, final_path)
                print(f"✅ ESP32 firmware ready at {final_path}")
            else:
                print("❌ Missing one or more ESP32 .bin files — skipping merge.")

        elif "esp8266" in mcu:
            app = os.path.join(build_dir, "firmware.bin")
            final_path = os.path.join(firmware_dir, "esp8266_firmware.bin")

            if os.path.isfile(app):
                shutil.copy(app, final_path)
                print(f"✅ Copied ESP8266 firmware to {final_path}")
            else:
                print("❌ Missing ESP8266 firmware.bin — skipping copy.")

        else:
            print(f"⚠️ Unsupported MCU type: {mcu} — skipping firmware handling.")
    except Exception as e:
        print(f"❌ Error during firmware handling: {e}")


env.AddPostAction("buildprog", after_build)

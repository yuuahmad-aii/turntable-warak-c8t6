# Kontrol Motor Stepper STM32F103C8T6 dengan Akselerasi/Deselerasi dan Penyimpanan Flash

Proyek ini adalah firmware untuk mikrokontroler STM32F103C8T6 (umumnya dikenal sebagai BluePill) untuk mengontrol motor stepper. Fitur utamanya meliputi pengaturan kecepatan, arah, enable/disable motor, akselerasi dan deselerasi yang mulus, penyimpanan pengaturan ke flash internal, dan konfigurasi parameter melalui USB Virtual COM Port (VCP). Kecepatan dan arah motor saat ini ditampilkan pada layar 7-segmen 4-digit yang dikendalikan oleh IC TM1637.

<!-- [title](https://www.example.com) -->


![turntable-warak (1).jpg](https://github.com/yuuahmad-aii/turntable-warak-c8t6/blob/ddb2357697330bfb17e6dda4644764bab31d2d37/turntable-warak%20(1).jpg)
![turntable-warak (2).jpg](https://github.com/yuuahmad-aii/turntable-warak-c8t6/blob/6aa412e21622da010dca6b34cc4982bb0669dbf2/turntable-warak%20(2).jpg)

## Fitur Utama

* Kontrol motor stepper (Pulse, Direction, Enable).
* Penyesuaian kecepatan target motor stepper melalui tombol.
* Perubahan arah putaran motor melalui tombol.
* Enable/Disable driver motor stepper melalui tombol.
* Implementasi akselerasi dan deselerasi untuk perubahan kecepatan yang lebih halus.
* Penyimpanan pengaturan ke Flash: Menyimpan kecepatan target terakhir, arah, status enable saat startup, dan parameter akselerasi/deselerasi.
* Pengambilan pengaturan dari Flash saat startup.
* Konfigurasi parameter (delay akselerasi/deselerasi) secara dinamis melalui USB Virtual COM Port (VCP).
* Tampilan 7-segmen 4-digit (TM1637) untuk menunjukkan:
    * Digit pertama (paling kiri): Arah motor (ditampilkan sebagai `-` untuk satu arah, dan kosong untuk arah lainnya).
    * Tiga digit berikutnya: Level kecepatan motor saat ini (nilainya 0-100).
* Indikator LED (pada BluePill) untuk status motor berjalan.

## Konfigurasi Pin yang Digunakan

Berikut adalah konfigurasi pin default yang digunakan dalam program ini.

* **Motor Stepper:**
    * `PULSE_PIN_Pin` (GPIOA, PA3): Output sinyal pulse ke driver stepper.
    * `DIR_PIN_Pin` (GPIOA, PA1): Output arah putaran motor.
    * `ENABLE_PIN_Pin` (GPIOA, PA2): Output untuk mengaktifkan/menonaktifkan driver stepper (Active LOW).
* **Tombol Kontrol:**
    * `BTN_SPEED_UP_Pin` (GPIOB, PB6): Input untuk menaikkan target kecepatan (Active LOW, internal Pull-up).
    * `BTN_SPEED_DOWN_Pin` (GPIOB, PB7): Input untuk menurunkan target kecepatan (Active LOW, internal Pull-up).
    * `BTN_DIR_Pin` (GPIOB, PB8): Input untuk mengubah arah putaran motor (Active LOW, internal Pull-up).
    * `BTN_ENABLE_Pin` (GPIOB, PB9): Input untuk mengaktifkan/menonaktifkan target motor (Active LOW, internal Pull-up).
    * `BTN_SAVE_SETTINGS_Pin` (GPIOA, PA0): Input untuk menyimpan pengaturan saat ini ke flash (Active HIGH, internal Pull-down).
* **Display TM1637 (7-Segment):**
    * `TM1637_CLK_Pin` (GPIOB, PB10): Output pin Clock untuk TM1637.
    * `TM1637_DIO_Pin` (GPIOB, PB11): Output pin Data I/O untuk TM1637.
* **Indikator LED:**
    * `LED_BLUEPILL_Pin` (GPIOB, PB2): Menyala ketika motor sedang berputar (mengirim pulse).

* **Timer:**
    * `TIM2`: Digunakan untuk menghasilkan interupsi periodik yang mengontrol pulse motor stepper. Frekuensi interupsi menentukan kecepatan motor.

## Cara Menggunakan Alat

1.  **Koneksi Perangkat Keras:**
    * Hubungkan driver motor stepper ke pin `PULSE`, `DIR`, dan `ENABLE` pada STM32.
    * Hubungkan display TM1637 ke pin `TM1637_CLK` dan `TM1637_DIO`, serta VCC dan GND.
    * Hubungkan 5 tombol ke pin yang sesuai (`BTN_SPEED_UP`, `BTN_SPEED_DOWN`, `BTN_DIR`, `BTN_ENABLE`, `BTN_SAVE_SETTINGS`). Pastikan konfigurasi pull-up/pull-down sesuai dengan logika tombol.
    * Berikan daya pada board STM32 BluePill (melalui USB).
2.  **Startup:**
    * Saat dinyalakan, STM32 akan mencoba memuat pengaturan yang tersimpan dari flash memory.
    * Jika ini adalah penggunaan pertama atau data flash korup (checksum error), pengaturan default akan dimuat.
    * Display TM1637 akan menampilkan kecepatan awal (dari 0) dan arah.
3.  **Operasi Tombol:**
    * **Tombol Naikkan Kecepatan (`BTN_SPEED_UP_Pin`):** Menaikkan *target* level kecepatan motor. Motor akan berakselerasi ke level ini.
    * **Tombol Turunkan Kecepatan (`BTN_SPEED_DOWN_Pin`):** Menurunkan *target* level kecepatan motor. Motor akan berdeselerasi ke level ini, atau berhenti jika target mencapai 0.
    * **Tombol Arah (`BTN_DIR_Pin`):** Mengubah target arah putaran motor. Perubahan arah efektif saat motor berhenti dan mulai lagi atau jika diubah saat berjalan (tergantung implementasi driver stepper).
    * **Tombol Enable/Disable (`BTN_ENABLE_Pin`):** Mengubah status *target enable* motor.
        * Jika diubah ke ON, motor akan berakselerasi ke `g_target_speed_level` (jika target speed > 0).
        * Jika diubah ke OFF, motor akan berdeselerasi hingga berhenti.
    * **Tombol Simpan Pengaturan (`BTN_SAVE_SETTINGS_Pin`):** Menekan tombol ini akan menyimpan pengaturan saat ini (target kecepatan, arah, status enable on startup, delay akselerasi, delay deselerasi) ke flash memory internal STM32.
4.  **Indikator LED:** 
    * LED pada board akan menyala jika motor sedang aktif (mengirim pulse) dan padam jika motor berhenti.
5.  **Display TM1637:**
    * Digit paling kiri: Menampilkan `-` jika `g_motor_direction` adalah 1 (misalnya CCW), dan kosong jika `g_motor_direction` adalah 0 (misalnya CW).
    * Tiga digit di kanan: Menampilkan `g_current_speed_level` (0 hingga `MAX_SPEED_LEVEL`, nilinya 0-100).

## Pengaturan Parameter melalui Serial Monitor (USB VCP)
 hubungakan STM32F103 PC melalui USB. STM32 akan terdeteksi sebagai Virtual COM Port. Gunakan terminal serial (seperti PuTTY, TeraTerm, Arduino IDE Serial Monitor) untuk mengirim perintah konfigurasi.

* **Baud Rate:** Default USB CDC tidak memerlukan setting baud rate manual di sisi PC.
* **Format Perintah:** Kirim perintah teks diakhiri dengan Enter (Newline).

* **Perintah yang Didukung:**
    * `$ACCEL=value`: Mengatur delay langkah akselerasi dalam milidetik (ms). Nilai yang lebih kecil berarti akselerasi lebih cepat.
        * Contoh: `$ACCEL=30` (delay 30ms per perubahan 1 level kecepatan saat akselerasi).
        * Rentang nilai: 10-10000.
    * `$DECEL=value`: Mengatur delay langkah deselerasi dalam milidetik (ms). Nilai yang lebih kecil berarti deselerasi lebih cepat.
        * Contoh: `$DECEL=60` (delay 60ms per perubahan 1 level kecepatan saat deselerasi).
        * Rentang nilai: 10-10000.
    * `$$`: Menampilkan parameter operasional saat ini ke terminal serial (TargetSpeed, CurrentSpeed, Dir, EnabledTarget, Accel, Decel).
    * `$SAVE`: Memicu penyimpanan pengaturan saat ini (target kecepatan, arah, status enable on startup, delay akselerasi, delay deselerasi) ke flash memory, sama seperti menekan tombol fisik PA0.

**Catatan:**
* Parameter kecepatan (`PULSE_TIMER_BASE_ARR`, `PULSE_TIMER_ARR_STEP`, `MAX_SPEED_LEVEL`, `MINIMUM_TIMER_ARR_VALUE`) di dalam kode mungkin perlu disesuaikan berdasarkan karakteristik motor stepper dan frekuensi clock timer (TIM2) untuk mendapatkan rentang kecepatan yang diinginkan.
* Alamat flash `0x0800FC00` adalah contoh untuk STM32F103C8T6 dengan flash 64KB (halaman terakhir). Pastikan alamat ini valid dan tidak bertabrakan dengan kode program.
* Firmware menggunakan `printf` untuk output debug melalui USB VCP. Pastikan proyek STM32CubeIDE dikonfigurasi untuk me-redirect `printf` ke USB CDC jika ingin melihat pesan debug ini.
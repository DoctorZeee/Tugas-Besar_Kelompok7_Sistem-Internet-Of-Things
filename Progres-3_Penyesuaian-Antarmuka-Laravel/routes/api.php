<?php

// Sengaja kosong. Dahulu /api/lift/* dan /api/inventory/* terbuka tanpa autentikasi, sehingga
// siapa pun di internet dapat menggerakkan lift. Semua endpoint sekarang ada di routes/web.php
// di balik login. ESP32 tidak memakai REST: ia berkomunikasi lewat MQTT.

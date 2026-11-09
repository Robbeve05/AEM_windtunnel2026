from pathlib import Path
p=Path('ESP32/include/AppConfig.h');s=p.read_text().replace('#define MOTOR_MODEL_TORQUE_TO_V20_ENABLED (1)','#define MOTOR_MODEL_TORQUE_TO_V20_ENABLED (0)').replace('#define FREQUENCY_MODEL_VALIDATED 0','#define FREQUENCY_MODEL_VALIDATED 1').replace('#define FREQUENCY_MODEL_COMMISSIONED 0','#define FREQUENCY_MODEL_COMMISSIONED 1');p.write_text(s)

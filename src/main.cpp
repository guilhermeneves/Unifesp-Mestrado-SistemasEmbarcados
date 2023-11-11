/*
-> Sistemas Embarcados - Unifesp 2023 - PPGCC
-> Atividade 3
-> Aluno: Luiz Guilherme Neves da Silva
-> Professor: Sergio Ronaldo Barros dos Santos

Funcionalidades Implementadas neste Programa
1. Configuração do timer RTOS (xTimerCreate) de funcionamento Centrífuga via KeyPad na opção A e envio para execução na fila A.
2. Configuração de hora/minuto do RTC via KeyPad na opção B e envio para execução no RTC via fila B.
3. Configuração velocidade Motor Centrífuga nos botões 1,2,3 e 4 e envio para execução na fila C.
4. Start/Stop do motor da Centrífuga nos Botões ISR 1 e 2 com controle de interrupção e sistema de notificação (similar a semaforo).

Requisitos atendidos:
-> O motor deve girar no sentido horário durante o tempo de funcionamento inserido no keypad pelo usuário.
-> O display LCD deve mostrar o horário computado pelo módulo RTC.
-> Os displays 7-segmentos devem mostrar a contagem decrescente de tempo de funcionamento da centrífuga (minutos e segundo).
-> O tempo de funcionamento e o horário devem ser ajustados
-> O tempo de funcionamento e o horário devem ser ajustados usando o keypad.
-> Os pushbuttons devem ser usados para selecionar a velocidade de rotação do motor.
-> Cada pushbutton deve selecionar uma velocidade diferente de funcionamento. A centrífuga deve possui, no mínimo, 4 velocidades diferentes.
-> Cada led deve ser acionado quando uma determinada velocidade de rotação for ativada. Utilize no mínimo 4 leds.
-> Além dos pushbuttons de controle de velocidade, utilize mais dois botões, um para ligar e um outro para desligar o equipamento.
-> O buzzer deve apitar 3 vezes (1 segundo ligado e 1 segundo desligado) assim que finalizar o tempo de funcionamento.
-> Outras funcionalidades são bem-vinda, sua implementação será premiada com um acréscimo da nota!
*/

#include <TM1637Display.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <Keypad.h>
#include <Stepper.h>
#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <timers.h>
#include <limits.h>

/**** Constantes ****/

// Constantes para Display OLED SSD1306
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

// Constantes para display TM1637 7 segmentos
#define CLK 12
#define DIO 13

// Constantes para Keypad
#define ROWS 4
#define COLS 4

// Constantes para Motor
#define MOTOR_STEPS 200
#define DIR 2
#define STEP 3

// Constantes para os LED's
#define LED_1 A7
#define LED_2 A6
#define LED_3 A5
#define LED_4 A4

// Constantes para os Botões
#define BTN_1 23
#define BTN_2 25
#define BTN_3 27
#define BTN_4 29
#define BTN_ISR_1 18
#define BTN_ISR_2 19

// Constante para Buzzer
#define BUZZER 37


/**** Inicialização de Estrutura de Dados dos Periféricos ****/

// Inicialização Estrutura dados Display's
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
TM1637Display tmDisplay(CLK, DIO);

// Inicialização Estrutura dados RTC
RTC_DS1307 rtc;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {A15, A14, A13, A12};
byte colPins[COLS] = {A11, A10, A9, A8};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
void KeyPadUserInput(void *pvParameters);

// Inicialização Estrutura dados Motor de Passo
Stepper stepper(MOTOR_STEPS, DIR, STEP);

// Inicialização Estrutura dados Led's e Botões
const int ledPins[] = {LED_1, LED_2, LED_3, LED_4};
const int buttonPins[] = {BTN_1, BTN_2, BTN_3, BTN_4, BTN_ISR_1, BTN_ISR_1};


/**** Variáveis Globais de Suporte ao Programa ****/

// Variavel Compartilhada Controle Motor da Centrifuga
static bool motorRunning = false;
static bool timerCentrifugeEnabled = false;

// Variavel Compartilhada para o armazenamento de digitos recebidos diretamente via KeyPad
// Utilizada para ambos prefixos:
// A: Configura tempo operação da Centrifuga.
// B: Configura Horario RTC.
char sharedChars[6] = {0};

// Variável para controle de tempo para impressão no display
volatile unsigned long endTime;


/**** Estrutura de dados RTOS ****/

// Declaração das Tasks
void CentrifuguePowerTimerTask(void *pvParameters);
void CentrifugePowerOffCallback(TimerHandle_t xTimer);
void PrintTimeLeftTask(void *pvParameters);
void TimeUpdateTask(void *pvParameters);
void DisplayTimeTask(void *pvParameters);
void CentrifugePowerTask(void *pvParameters);
void CentrifugueSpeedTask(void *pvParameters);
void ButtonTask(void *pvParameters);

// queueA: Configura tempo operação da Centrifuga.
// queueB: Configura Horario RTC.
// queueC: Configura Velocidade Centrifuga.
static QueueHandle_t queueA;
static QueueHandle_t queueB;
static QueueHandle_t queueC;


// Handles
TaskHandle_t hCentrifugePowerTask;
TimerHandle_t centrifugeTimer = NULL;


// Interrupção para o Botão Start Centrifuga (Utilizando Notificação 0)
void StartISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xTaskNotifyFromISR(hCentrifugePowerTask, 0, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken == pdTRUE) {
   portYIELD_FROM_ISR();
  }
  xTaskResumeFromISR(hCentrifugePowerTask);
}

// Interrupção para o Botão Stop Centrifuga (Utilizando Notificação 1)
void StopISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xTaskNotifyFromISR(hCentrifugePowerTask, 1, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken != pdFALSE) {
    portYIELD_FROM_ISR();
  }
}


void setup() {
  /**** Inicialização dos componentes do programa ****/

  // Inicia Comunicação Serial
  Serial.begin(9600);
  
  // Inicia RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }
  
  // Configura luminosidade do display 7 segmentos
  tmDisplay.setBrightness(0x0f);
  
  
  // Inicializa Leds
  for (int i = 0; i < 4; i++) {
    pinMode(ledPins[i], OUTPUT);
  }
  
  // Inicializa Botões com PULLUP, devido a configuração do CI
  for (int i = 0; i < 6; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  //Adiciona interrupção nos pinos start e stop quando vai de HIGH para LOW (FALLING)
  attachInterrupt(digitalPinToInterrupt(buttonPins[4]), StartISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(buttonPins[5]), StopISR, FALLING);
  
  // Inicializa Buzzer
  pinMode(BUZZER, OUTPUT);
  
  // Configura pinos do motor como saída
  pinMode(STEP, OUTPUT);
  pinMode(DIR, OUTPUT);

  // Inicializa e Cria as filas
  queueA = xQueueCreate(10, sizeof(char*));
  queueB = xQueueCreate(10, sizeof(char*));
  queueC = xQueueCreate(10, sizeof(int*));


  /**** Criação Tasks RTOS ****/

  // Tarefa para lidar com Input no KeyPad
  xTaskCreate(
    KeyPadUserInput,     // Função que implementa a Tarefa
    "KeypadTask",        // Nome da Tarefa
    256,                 // Stack size in words, not bytes. (SRAM)
    NULL,                // Parametros para Tarefa.
    1,                   // Prioridade.
    NULL);               // Ponteiro para o handle da tarefa criada.
  // Tarefa para lidar com controle de liga/desliga da centrifuga
  xTaskCreate(
    CentrifuguePowerTimerTask,     
    "CentrifugePowerTask",         
    256,                      
    NULL,                     
    1,                        
    NULL                      
  );
  // Tarefa para mostrar tempo faltante para centrifuga desligar
  xTaskCreate(
    PrintTimeLeftTask,
    "TimeLeft",
    256,
    NULL,
    1,
    NULL);
  // Tarefa para atualização da hora e minuto no RTC recebido pela filaB
  xTaskCreate(
    TimeUpdateTask,
    "TimeUpdate",    
    256,      
    NULL,     
    1,            
    NULL    
  );
  // Tarefa para mostrar o tempo do RTC no display OLED.
  xTaskCreate(
    DisplayTimeTask,        
    "DisplayTime",
    512,       
    NULL,              
    1,                     
    NULL             
    );
  //Tarefa que receba notificação via ISR dos botões start/stop para alterar variável global de controle do motor
  xTaskCreate(
    CentrifugePowerTask,        
    "Centrifuge",
    256,      
    NULL,             
    1,                    
    &hCentrifugePowerTask
  );
  // Tarefa que controla o envio de mensagens para a filaC baseado na seleção do botão de velocidade
  xTaskCreate(
    ButtonTask,
    "ButtonTask",
    256,
    NULL,
    1,
    NULL);
  // Tarefa que monitora variáveis globais para atuação no motor
  xTaskCreate(
    CentrifugueSpeedTask,
    "CentrifugueSpeedTask",
    256,
    NULL,
    1,
    NULL);
  
}

void loop() {
}

 /**** Funções Suporte ****/

// Avalia se os últimos 4 caracteres são no formato NN:NN / Função para minuto:segundo ou hora:minuto
bool isValidTimeFormat(const char *chars) {
  for (int i = 1; i < 5; i++) {
    if (!isdigit(chars[i])) return false;
  }
  // Conversão para inteiro e conversão decimal do digito
  int minutes = (chars[1] - '0') * 10 + (chars[2] - '0');
  int seconds = (chars[3] - '0') * 10 + (chars[4] - '0');

  // Validação para minuto e segundo, verificação extra necessária para hora e minuto.
  return minutes < 60 && seconds < 60;
}

// Atualiza RTC baseado no input do usuário
void updateRTCTime(int hours, int minutes) {
    DateTime now = rtc.now();
    DateTime updatedTime = DateTime(now.year(), now.month(), now.day(), hours, minutes, now.second());
    rtc.adjust(updatedTime);
}

/**** Implementação Tarefas RTOS ****/

void KeyPadUserInput(void *pvParameters) {
  (void) pvParameters;
  char key;
  
  while (1) {
    // Leitura não-bloqueante
    key = keypad.getKey();

    if (key == NO_KEY) {
      vTaskDelay(50 / portTICK_PERIOD_MS);
      continue;
    }

    // Se 'A' ou 'B' então reseta
    if (key == 'A' || key == 'B') {
      memset(sharedChars, 0, sizeof(sharedChars));
      sharedChars[0] = key;
      const char* typeSelection = (key == 'A') ? "Motor" : "RTC";
      Serial.println("Controle Iniciado para Set " + String(typeSelection));
    }
    // Se '#' recebido, avalia input em memória para envio na respectiva fila
    else if (key == '#') {
      if (isValidTimeFormat(sharedChars)) {
        char *formattedTime = (char *)pvPortMalloc(5); // alocação de 5 bytes, cada char 1 byte.
        if (formattedTime != NULL) {
          memcpy(formattedTime, sharedChars + 1, 4); // Copia os últimos 4 chars.
          formattedTime[4] = '\0';
          if (sharedChars[0] == 'A') {
            xQueueSend(queueA, (void *)&formattedTime, portMAX_DELAY);
            Serial.println("Dado: " + String(formattedTime) + " Enviado para FilaA");
          } else if (sharedChars[0] == 'B') {
            xQueueSend(queueB, (void *)&formattedTime, portMAX_DELAY);
            Serial.println("Dado: " + String(formattedTime) + " Enviado para FilaB");
          }
        }
      //Reseta variável global
      memset(sharedChars, 0, sizeof(sharedChars));
      } else {
        Serial.print("#ERRO: Utilizar formato MMSS (MM=00-59 e SS=00-59) para A e HHMM (HH=00-23 e MM=00-59) para B! Valor entrada:");
        Serial.println(&sharedChars[1]);
      }
    }
    else if (isdigit(key)) {
      int length = strlen(sharedChars);
      int max_length = sizeof(sharedChars) - 1;
      if (length == 0) {
        Serial.println("Nenhuma Função Selecionada. Por favor, selecione A ou B.");
      }
      else if (length < max_length) {
        // Se houver espaço apenas adiciona digito
        sharedChars[length] = key;
        sharedChars[length + 1] = '\0';
      } else {
        // Se não houver espaço, faz o shift para a esquerda
        memmove(sharedChars + 1, sharedChars + 2, max_length - 2);
        sharedChars[max_length - 1] = key;
      }
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void CentrifuguePowerTimerTask(void *pvParameters) {
  char *timeValue; // Variável para receber valor da fila

  // Inicializa o timer sem o start!
  centrifugeTimer = xTimerCreate(
    "CentrifugeTimer",
    pdMS_TO_TICKS(1000),
    pdFALSE,                          // Timer sem auto-reload.
    (void *)0,
    CentrifugePowerOffCallback        // Função de callback para desligar a centrifuga
  );

  while (1) {
    if (xQueueReceive(queueA, (void *)&timeValue, portMAX_DELAY) == pdPASS) {
      Serial.println("Dado Recebido pela Task CentrifuguePowerTimerTask na fila queueA");
      int minutes = (timeValue[0] - '0') * 10 + (timeValue[1] - '0');
      int seconds = (timeValue[2] - '0') * 10 + (timeValue[3] - '0');
      unsigned long totalSeconds = minutes * 60 + seconds;
      DateTime now = rtc.now();
      endTime = now.unixtime() + totalSeconds;
      
      TickType_t timerTicks = pdMS_TO_TICKS(totalSeconds * 1000);
      // Tentativa de alterar a quantidade de ticks do Timer
      if (xTimerChangePeriod(centrifugeTimer, timerTicks, 100) != pdPASS) {
        Serial.println("Falha ao atualizar Ticks do Timer!");
        timerCentrifugeEnabled = false;
      }

      // Inicia o Timer com nova quantidade de Ticks configurada anteriormente
      if (xTimerStart(centrifugeTimer, 0) == pdPASS) {
        Serial.print("Timer Setado para Total Segundos: ");
        Serial.println(totalSeconds);
        timerCentrifugeEnabled = true;
      }
    }
  }
}

// Função Callback Timer
void CentrifugePowerOffCallback(TimerHandle_t xTimer) {
  int buzzerToggleCount = 0;
  Serial.println("Centrifuge Shutting Down!");

  while (buzzerToggleCount < 6) {
    digitalWrite(BUZZER, buzzerToggleCount % 2);
    vTaskDelay(pdMS_TO_TICKS(1000));
    buzzerToggleCount++;
  }
  timerCentrifugeEnabled = false;
  motorRunning = false;
  Serial.println("Centrifuge Stopped!");
}

void PrintTimeLeftTask(void *pvParameters) {
  while (1) {
    DateTime now = rtc.now();
    unsigned long nowTime = now.unixtime();
    unsigned long timeLeft = endTime > nowTime ? endTime - nowTime : 0;

    // Converte o tempo restante para minutos e segundos
    int minutesLeft = timeLeft / 60;
    int secondsLeft = timeLeft % 60;
    // Mostra o tempo no display
    tmDisplay.showNumberDecEx(minutesLeft * 100 + secondsLeft, 0b11100000, true);

    // Atualiza a cada segundo
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void TimeUpdateTask(void *pvParameters) {
    int hours, minutes;
    char *timeValue;

    while (1) {
        if (xQueueReceive(queueB, &timeValue, portMAX_DELAY) == pdPASS) {
            Serial.println("Mensagem Recebida na FilaB pela Task TimeUpdateTask");

            hours = (timeValue[0] - '0') * 10 + (timeValue[1] - '0');
            minutes = (timeValue[2] - '0') * 10 + (timeValue[3] - '0');

            updateRTCTime(hours, minutes);

            // Libera memoria alocada depois de atualizar RTC
            vPortFree(timeValue);

            Serial.print("Updated RTC Time: ");
            Serial.print(hours);
            Serial.print(":");
            Serial.println(minutes);
        }
    }
}

void DisplayTimeTask(void *pvParameters) {
    // Inicializa Display
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    while(1) {
        DateTime now = rtc.now();

        display.clearDisplay();
        display.setTextSize(4); 
        display.setCursor(0, 0); 
        
        // Mostra tempo em HH:MM:SS
        if(now.hour() < 10) display.print('0');
        display.print(now.hour(), DEC);
        display.print(':');
        if(now.minute() < 10) display.print('0');
        display.print(now.minute(), DEC);
        display.print(':');
        if(now.second() < 10) display.print('0');
        display.print(now.second(), DEC);
        
        display.display();

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void CentrifugePowerTask(void *pvParameters) {
  uint32_t notificationValue;
  const TickType_t xMaxBlockTime = pdMS_TO_TICKS(500);
  while(1) {
    if(xTaskNotifyWait(0x00, ULONG_MAX, &notificationValue, xMaxBlockTime) == pdTRUE) {
      if(notificationValue == 0) {
        // Botão Start Pressionado
        if(!motorRunning) {
          // Debounce
          vTaskDelay(pdMS_TO_TICKS(50));
          if(digitalRead(buttonPins[4]) == HIGH) {
            if(timerCentrifugeEnabled == true) {
              Serial.println("Centrifuga Iniciada!");
              motorRunning = true;
            } else {
              Serial.println("Não foi possível ligar a Centrifuga, por favor, configure o Timer antes!");
            }
          }
        }
      } else if(notificationValue == 1) {
        // Botão Stop Pressionado
        if(motorRunning) {
          // Debounce
          vTaskDelay(pdMS_TO_TICKS(50));
          if(digitalRead(buttonPins[5]) == HIGH) {
            Serial.println("Centrifuga Parada!");
            motorRunning = false;
          }
        }
      }
    }
  }
}

void CentrifugueSpeedTask(void *pvParameters) {
  int motorSpeed;
  const TickType_t xMaxBlockTime = pdMS_TO_TICKS(10);
  stepper.setSpeed(60);
  while(1) {
    if(motorRunning){
      stepper.step(-1);
    }
    if(xQueueReceive(queueC, (void *)&motorSpeed, xMaxBlockTime) == pdPASS) {
      Serial.print("Task Recebida na fila queueC pela Task: CentrifugueSpeedTask, Nova Velocidade:");
      Serial.println(motorSpeed);
      stepper.setSpeed(motorSpeed);
    }
  }
}

void ButtonTask(void *pvParameters) {
  TickType_t lastPressTime[4] = {0};
  bool lastButtonState[4] = {HIGH, HIGH, HIGH, HIGH};
  TickType_t debounceTime = pdMS_TO_TICKS(50);
  TickType_t ignoreTime = pdMS_TO_TICKS(500);

  while (1) {
    for (int i = 0; i < 4; i++) {
      bool buttonState = digitalRead(buttonPins[i]);

      if (buttonState == LOW && lastButtonState[i] == HIGH) {
        TickType_t currentTime = xTaskGetTickCount();
        if ((currentTime - lastPressTime[i]) >= debounceTime) {
          int motorSpeed = 0;

          switch (i) {
            case 0:
              motorSpeed = 10;
              break;
            case 1:
              motorSpeed = 35;
              break;
            case 2:
              motorSpeed = 60;
              break;
            case 3:
              motorSpeed = 120;
              break;
            default:
              break;
          }
          Serial.print("Nova Velocidade Enviada para FilaC. MotorSpeed:");
          Serial.println(motorSpeed);
          for (int j = 0; j < 4; j++) {
            if (3 - j == i) {
              digitalWrite(ledPins[j], HIGH);
            } else {
              digitalWrite(ledPins[j], LOW);
            }
          }
          xQueueSend(queueC, (void *)&motorSpeed, portMAX_DELAY);

          lastPressTime[i] = currentTime;
          lastButtonState[i] = buttonState;

          vTaskDelay(ignoreTime);
        }
      } else {
        lastButtonState[i] = buttonState;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

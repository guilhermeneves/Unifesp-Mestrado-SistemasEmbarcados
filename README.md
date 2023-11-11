# Sistemas Embarcados

## Unifesp - PPGCC

### Atividade 3

`Sistemas Embarcados - Unifesp 2023 - PPGCC`

Aluno: Luiz Guilherme Neves da Silva

Professor: Sergio Ronaldo Barros dos Santos

**Funcionalidades Implementadas neste Programa**

1. Configuração do timer RTOS (xTimerCreate) de funcionamento Centrífuga via KeyPad na opção A e envio para execução na fila A.
2. Configuração de hora/minuto do RTC via KeyPad na opção B e envio para execução no RTC via fila B.
3. Configuração velocidade Motor Centrífuga nos botões 1,2,3 e 4 e envio para execução na fila C.
4. Start/Stop do motor da Centrífuga nos Botões ISR 1 e 2 com controle de interrupção e sistema de notificação (similar a semaforo).

**Requisitos atendidos:**
- O motor deve girar no sentido horário durante o tempo de funcionamento inserido no keypad pelo usuário.
-  O display LCD deve mostrar o horário computado pelo módulo RTC.
- Os displays 7-segmentos devem mostrar a contagem decrescente de tempo de funcionamento da centrífuga (minutos e segundo).
- O tempo de funcionamento e o horário devem ser ajustados
- O tempo de funcionamento e o horário devem ser ajustados usando o keypad.
- Os pushbuttons devem ser usados para selecionar a velocidade de rotação do motor.
- Cada pushbutton deve selecionar uma velocidade diferente de funcionamento. A centrífuga deve possui, no mínimo, 4 velocidades diferentes.
- Cada led deve ser acionado quando uma determinada velocidade de rotação for ativada. Utilize no mínimo 4 leds.
- Além dos pushbuttons de controle de velocidade, utilize mais dois botões, um para ligar e um outro para desligar o equipamento.
- O buzzer deve apitar 3 vezes (1 segundo ligado e 1 segundo desligado) assim que finalizar o tempo de funcionamento.
- Outras funcionalidades são bem-vinda, sua implementação será premiada com um acréscimo da nota!

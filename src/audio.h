#ifndef AUDIO_H
#define AUDIO_H

// ============================================================
// AUDIO PROCEDURAL
//
// Nenhum arquivo de som: cada som e' SINTETIZADO em codigo (ondas,
// ruido, filtros, envelopes). A saida pro alto-falante usa a biblioteca
// miniaudio (um unico .h, dominio publico); a sintese e a mixagem sao
// nossas (audio.cpp).
//
// Como o jogo conversa com o audio:
//   - PARAMETROS CONTINUOS (audioSetParams): o jogo descreve, a cada
//     frame, o que esta' acontecendo (onde o monstro esta', se a porta
//     esta' girando, o nivel de perigo...). O audio ajusta os sons
//     continuos de acordo (passos, zumbido, batimentos, ronco...).
//   - EVENTOS (audioEvent): coisas que acontecem num instante (clique da
//     lanterna, batida da porta, susto...), tocadas uma vez.
//
// Sem dispositivo de audio (ou compilando com -DNO_AUDIO) o jogo roda
// normalmente, so' que mudo.
// ============================================================

enum AudioEvent {
    EVT_FLASH_ON,     // clique ao ligar a lanterna
    EVT_FLASH_OFF,    // clique ao desligar
    EVT_RELAY,        // rele da porta comecando a mexer
    EVT_DOOR_SLAM,    // porta fechando por completo (pancada)
    EVT_DOOR_CLUNK,   // porta chegando ao fim aberta (baque leve)
    EVT_MONITOR_ON,   // bipe subindo
    EVT_MONITOR_OFF,  // bipe descendo
    EVT_JUMPSCARE,    // o susto: grito + pancada grave
    EVT_POWER_OUT,    // a energia acabou
    EVT_CHIME,        // sino distante a cada "hora" da noite
    EVT_WIN,          // seis badaladas: amanheceu
    EVT_HEART_LUB,    // primeira batida do coracao
    EVT_HEART_DUB,    // segunda batida
    EVT_MENU_MOVE,    // tick ao mudar de item no menu
    EVT_MENU_SELECT   // confirmacao no menu
};

// Estado do jogo que importa pro som. O jogo preenche todo frame.
struct AudioParams {
    float master;        // volume geral, 0..1 (baixo nos menus)
    float danger;        // 0..1, nivel de perigo (mesmo valor da vinheta)
    float monsterDist;   // distancia ate' o monstro, em metros
    float monsterPan;    // -1 = esquerda, +1 = direita (relativo pra onde o jogador olha)
    float stepHz;        // passos por segundo (0 = o monstro esta' parado)
    float doorMoving;    // 1 = a porta esta' em movimento
    float flashLevel;    // 0 = lanterna desligada/apagada, 1 = plena
    float beamHit;       // 1 = o feixe esta' acertando o monstro
    float monitorOn;     // 1 = monitor ligado
    float monitorClose;  // 0..1, o quao perto o monstro esta' (acelera o bipe do monitor)
    float powerDead;     // 1 = bateria zerada
};

// Abre o dispositivo de audio. Devolve false se nao houver (o jogo segue mudo).
bool audioInit();
void audioShutdown();

void audioSetParams(const AudioParams& p);
void audioSetLamps(float lampA, float lampB);   // brilho (0..1) das 2 lampadas; o zumbido acompanha
void audioEvent(AudioEvent e);

// Gera "frames" amostras estereo intercaladas (L,R,L,R...) a 44100 Hz.
// E' o que o dispositivo chama; fica publico pra podermos testar a sintese
// sem placa de som (renderizando pra um arquivo).
void audioRender(float* out, int frames);

#endif
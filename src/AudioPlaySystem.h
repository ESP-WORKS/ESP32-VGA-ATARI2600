#ifndef audioplaysystem_h_
#define audioplaysystem_h_

#define DEFAULT_SAMPLESIZE   512  // 22050/50=443 samples per 20ms
#define DEFAULT_SAMPLERATE   22050

class AudioPlaySystem
{
public:
// NAO chamar begin() aqui: este objeto e' global e o construtor rodaria
// durante a static init, antes do setup()/video. O go.cpp chama
// audio.begin()/start() ao carregar o primeiro jogo (startGame).
AudioPlaySystem(void) { }
	void begin(void);
	void setSampleParameters(float clockfreq, float samplerate);
	void reset(void);
	void start(void);
	void stop(void);
	bool isPlaying(void);
	void sound(int C, int F, int V);  	
	void buzz(int size, int val);
	void step(void);  
};


#endif

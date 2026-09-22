#include "Mode.hpp"
#include "Sound.hpp"
#include "questions.hpp"
#include <glm/glm.hpp>
#include <string>

struct PlayMode : Mode
{
	PlayMode();
	virtual ~PlayMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	void next_question();
	void advance();
	void start_ending();

	float text_time = 0.0f;

	int phase = 0;
	float timer = 0.0f;
	int q = 0, asker = 0, answers_len = 0;
	int shown[4] = {0, 0, 0, 0};
	bool used[23] = {false};
	int done = 0, burst = 0;
	int score[3] = {0, 0, 0}, mood[3] = {0, 0, 0};
	char const *claims[F_COUNT] = {nullptr};
	std::string caught_line;
	int contradictions = 0, ending = 0;
	int intro_i = 0, ending_i = 0, last_glyph = 0, stinger = 0;
	float stinger_timer = 0.0f;
	bool talking = false;
	std::shared_ptr<Sound::PlayingSample> music;
	char const *name = nullptr;
};

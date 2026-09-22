#include "PlayMode.hpp"

#include "ColorTextureProgram.hpp"
#include "data_path.hpp"
#include "load_save_png.hpp"
#include "Sound.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// text shaping using harfbuzz
// glyph raster using freetype

static FT_Face face = nullptr;
static hb_font_t *hb_font = nullptr;
static GLuint text_vao = 0, text_vbo = 0;

struct Glyph
{
	GLuint tex;
	int w, h, bx, by;
};
static std::unordered_map<uint32_t, Glyph> glyphs;

struct Vert
{
	float x, y;
	glm::u8vec4 col;
	float u, v;
};

static Glyph const &glyph(uint32_t index)
{
	auto f = glyphs.find(index);
	if (f != glyphs.end())
		return f->second;

	FT_Load_Glyph(face, index, FT_LOAD_RENDER);
	FT_Bitmap &bm = face->glyph->bitmap;

	std::vector<glm::u8vec4> data(bm.width * bm.rows);
	for (uint32_t i = 0; i < bm.width * bm.rows; ++i)
		data[i] = glm::u8vec4(255, 255, 255, bm.buffer[i]);

	Glyph g;
	glGenTextures(1, &g.tex);
	glBindTexture(GL_TEXTURE_2D, g.tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bm.width, bm.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	g.w = bm.width;
	g.h = bm.rows;
	g.bx = face->glyph->bitmap_left;
	g.by = face->glyph->bitmap_top;
	return glyphs[index] = g;
}

static GLuint load_texture(std::string const &filename)
{
	glm::uvec2 size;
	std::vector<glm::u8vec4> data;
	load_png(data_path(filename), &size, &data, LowerLeftOrigin);
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, GLsizei(size.x), GLsizei(size.y), 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return tex;
}

static void sprite_draw(GLuint tex, float x, float y, float w, float h)
{
	Vert verts[6] = {
		{x, y, glm::u8vec4(0xff), 0.0f, 0.0f},
		{x + w, y, glm::u8vec4(0xff), 1.0f, 0.0f},
		{x + w, y + h, glm::u8vec4(0xff), 1.0f, 1.0f},
		{x, y, glm::u8vec4(0xff), 0.0f, 0.0f},
		{x + w, y + h, glm::u8vec4(0xff), 1.0f, 1.0f},
		{x, y + h, glm::u8vec4(0xff), 0.0f, 1.0f},
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
	glBindTexture(GL_TEXTURE_2D, tex);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

enum
{
	IDLE1,
	IDLE2,
	IDLE3,
	IDLE4,
	SIDEYE,
	LOOKATYOU,
	ANGRY,
	TALK1,
	TALK2,
	FRAMES
};

static char const *judge_files[3][FRAMES] = {
	{"sprites/minos/minos-idleanim1.png", "sprites/minos/minos-idleanim2.png", "sprites/minos/minos-idleanim3.png",
	 "sprites/minos/minos-idleanim4.png", "sprites/minos/minos-sideye.png", "sprites/minos/minos-lookatyou.png",
	 "sprites/minos/minos-angryidle.png", "sprites/minos/minos-talk1.png", "sprites/minos/minos-talk2.png"},
	{"sprites/rhama/rhama-idleanim1.png", "sprites/rhama/rhama-idleanim2.png", "sprites/rhama/rhama-idleanim3.png",
	 "sprites/rhama/rhama-idleanim4.png", "sprites/rhama/rhama-sideye.png", "sprites/rhama/rhama-lookatyou.png",
	 "sprites/rhama/rhama-angryidle.png", "sprites/rhama/rhama-talk1.png", "sprites/rhama/rhama-talk2.png"},
	{"sprites/aeacus/aeacus-idleanim1.png", "sprites/aeacus/aeacus-idleanim2.png", "sprites/aeacus/aeacus-idleanim3.png",
	 "sprites/aeacus/aeacus-idleanim4.png", "sprites/aeacus/aeacus-sideye.png", "sprites/aeacus/aeacus-lookatyou.png",
	 "sprites/aeacus/aeacus-angryidle.png", "sprites/aeacus/aeacus-talk1.png", "sprites/aeacus/aeacus-talk2.png"},
};

static GLuint bg_tex = 0;
static GLuint judge_tex[3][FRAMES];

static int const blink_seq[] = {IDLE2, IDLE3, IDLE4, IDLE3, IDLE2, IDLE1, -1};
static int const glance_seq[] = {LOOKATYOU, LOOKATYOU, LOOKATYOU, LOOKATYOU, IDLE1, -1};

struct Judge
{
	int const *seq = blink_seq;
	int step = 6;
	float timer = 0.0f, sideye = 0.0f;
	int frame = IDLE1;
};
static Judge judges[3];

enum
{
	E_AIDE = 5,
	E_ELYSIUM,
	E_ASPHODEL,
	E_TARTARUS,
	E_PRINCE
};

static char const *field_names[F_COUNT] = {"", "ORIGIN", "EDUCATION", "OCCUPATION", "WEALTH", "FAMILY", "GOD",
										   "GREATEST DEED", "WORST DEED", "CAUSE OF DEATH", "FAVORITE FOOD",
										   "FAVORITE ANIMAL", "TRAVEL"};

static char const *ending_names[] = {"", "HADES' SPECIAL PUNISHMENT", "REBORN AS A FARMED PIG",
									 "REBORN AS A BLIND RAT IN THE LABYRINTH", "REBORN AS A COCKROACH",
									 "HADES' AIDE", "ELYSIUM", "ASPHODEL", "TARTARUS", "REBORN AS A PRINCE"};

static char const *names[8] = {"ALEXIOS", "DEMETRIOS", "THEODOROS", "LYSANDROS",
							   "STRATON", "KALLIAS", "NIKIAS", "DAMON"};

struct Line
{
	int who;
	char const *text;
};

static Line const intro[] = {
	{0, "Welcome, dead soul, to Hades."},
	{0, "You have crossed the river."},
	{0, "Now you stand before the three judges."},
	{0, "We will weigh your life."},
	{1, "I am Rhadamanthys."},
	{1, "Beside me sits Minos, and beyond him the old one, Aeacus."},
	{1, "The Trial of Hades is a fair trial."},
	{1, "We ask you for the account of your life, and so it is only right that you receive ours first."},
	{2, "And try not to lie, friend."},
	{2, "Speak crooked and my brother will know it. He remembers every word a shade has ever spoken."},
	{2, "He's a bit too serious, that one."},
	{0, "Enough. Let the judgment begin."},
};
static int const intro_count = sizeof(intro) / sizeof(intro[0]);

static float const glyphs_per_second = 30.0f;

static Line const end_aide[] = {
	{0, "Your judgment ends."},
	{1, "I am impressed."},
	{1, "If you truly are who you say you are, you are extraordinary."},
	{2, "Brothers, let us invite him."},
	{0, "Yes, Aeacus."},
	{1, "Shade, join our side as Hades' aide."},
	{1, "This is the highest honor a dead soul can achieve."},
	{0, "Congratulations. You succeeded under our judgment."},
};

static Line const end_elysium[] = {
	{0, "Your judgment ends, good shade."},
	{2, "Friend, you did well. You will be placed highly."},
	{1, "Indeed. Shade, you will travel to Elysium, a field only for the best."},
	{0, "Enjoy your time, and live with honor, as you earned in your mortal life."},
};

static Line const end_asphodel[] = {
	{0, "Your judgment ends. Finally."},
	{2, "Friend, you may go join your fellows at Asphodel."},
	{1, "Enjoy your stay, shade."},
};

static Line const end_tartarus[] = {
	{0, "Enough."},
	{2, "You deserve nothing."},
	{1, "To the depths of Tartarus with you. Disgusting being."},
	{1, "Enjoy your stay, fiend."},
};

static Line const end_prince[] = {
	{0, "Interesting."},
	{0, "I think we ought to end your judgment here, under special circumstances."},
	{1, "I say we put him back, to prove himself."},
	{0, "I agree. I do not think you have proved yourself to earn Elysium."},
	{2, "And you are not deserving of Asphodel."},
	{1, "You are to be reborn as a prince."},
	{1, "Prove yourself in this second chance, shade. See you soon."},
};

static Line const end_hades[] = {
	{0, "Oh, is that so?"},
	{2, "You have blundered, friend."},
	{1, "Indeed. As the judges of Hades, we know his life."},
	{0, "Let us summon our lord and tell him of this. He will have a great laugh."},
	{1, "You will be subject to a very special punishment. One of a kind."},
	{1, "I pity you, shade."},
	{2, "Friend, you really should not have spoken of Hades in his house."},
	{0, "I wish you luck in your infinite torture."},
	{3, "I SEE."},
	{3, "IT WAS YOU WHO LIED IN MY NAME."},
	{3, "I WILL SEE TO YOUR PUNISHMENT PERSONALLY, WICKED SHADE."},
};

static Line const end_pig[] = {
	{0, "Oh no. Aeacus, evacuate. Rhadamanthys' wrath is incoming."},
	{2, "Indeed. Shade, you have blatantly lied to his face."},
	{1, "YOU DARE LIE? UNDER THE OATH PLACED UPON HADES' NAME?"},
	{1, "YOU FILTH."},
	{1, "I will give you a new life. As a pig, to be farmed for meat."},
	{1, "You will live among your brethren: filth and rot."},
	{1, "You deserve worse, shade. This is my kindness showing. LEAVE."},
	{2, "Sorry, friend. Learn from your mistakes. See you soon, pig."},
};

static Line const end_rat[] = {
	{2, "Oh no, shade. Did you not read the Book of Judges?"},
	{1, "You have blundered."},
	{0, "The labyrinth... interesting."},
	{0, "Knowing my past, you wish to talk about it?"},
	{0, "Fair. You have made your choices, so here come your consequences."},
	{0, "You will be reborn as a rat. Blind, in my labyrinth. An immortal rat."},
	{0, "Since you like labyrinths so much, go wander one for eternity."},
	{2, "Sorry, friend. Be more careful next time. Not that there will be one."},
};

static Line const end_cockroach[] = {
	{2, "Bugs are noble beings, shade. Maybe you should learn yourself."},
	{0, "You were given the Book of Judges, no? You should know better."},
	{1, "Aeacus' people were ants. He values bugs as great creatures."},
	{2, "I use my power to give you another life. This time, as a cockroach."},
	{2, "You will see how noble a creature an insect is."},
	{2, "See you soon, shade. Do not speak ill of insects."},
};

struct Script
{
	Line const *lines;
	int count;
};

static Script const endings[10] = {
	{nullptr, 0}, {end_hades, 11}, {end_pig, 8}, {end_rat, 8}, {end_cockroach, 6},
	{end_aide, 8}, {end_elysium, 4}, {end_asphodel, 3}, {end_tartarus, 4}, {end_prince, 7}};

static int angry_judge(int ending)
{
	return ending == E_PIG ? 1 : ending == E_RAT ? 0 : ending == E_COCKROACH ? 2 : -1;
}

static Load<Sound::Sample> voice_minos(LoadTagDefault, []() -> Sound::Sample const *
									   { return new Sound::Sample(data_path("audio/minos-voice.wav")); });
static Load<Sound::Sample> voice_rhama(LoadTagDefault, []() -> Sound::Sample const *
									   { return new Sound::Sample(data_path("audio/rhama-voice.wav")); });
static Load<Sound::Sample> voice_aeacus(LoadTagDefault, []() -> Sound::Sample const *
										{ return new Sound::Sample(data_path("audio/aeacus-voice.wav")); });
static Load<Sound::Sample> bg_sample(LoadTagDefault, []() -> Sound::Sample const *
									 { return new Sound::Sample(data_path("audio/bg.wav")); });
static Load<Sound::Sample> select_sample(LoadTagDefault, []() -> Sound::Sample const *
										 { return new Sound::Sample(data_path("audio/select.wav")); });

static Sound::Sample const &voice(int who)
{
	return who == 1 ? *voice_rhama : who == 2 ? *voice_aeacus : *voice_minos;
}

enum
{
	NAMING,
	INTRO,
	ASKING,
	ANSWERING,
	REACTING,
	CHOOSING,
	OVER
};

static int judge_frame(int i, int asker, bool talking, float text_time, int ending)
{
	if (asker == i && talking)
		return (int(text_time * 8.0f) % 2) ? TALK2 : TALK1;
	if (i == angry_judge(ending))
		return ANGRY;
	if (judges[i].sideye > 0.0f)
		return SIDEYE;
	if (asker == i)
		return LOOKATYOU;
	return judges[i].frame;
}

static void judge_update(Judge &j, float elapsed)
{
	j.sideye -= elapsed;
	j.timer -= elapsed;
	if (j.timer > 0.0f)
		return;
	if (j.seq[j.step] != -1)
	{
		j.frame = j.seq[j.step++];
		j.timer = 0.15f;
		return;
	}
	j.frame = IDLE1;
	j.seq = (rand() % 2) ? blink_seq : glance_seq;
	j.step = 0;
	j.timer = 2.0f + float(rand() % 400) / 100.0f;
}

static std::vector<std::string> wrap(std::string const &s, size_t n)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < s.size();)
	{
		size_t end = i + n < s.size() ? s.rfind(' ', i + n) : s.size();
		out.push_back(s.substr(i, end - i));
		i = end + 1;
	}
	return out;
}

void text_draw(std::string const &line, float x, float y, glm::u8vec4 color, int reveal, float scale = 1.0f)
{
	if (!face)
	{
		{ //load m6x11 font, give to harfbuzz
			FT_Library ft;
			FT_Init_FreeType(&ft);
			FT_New_Face(ft, data_path("m6x11.ttf").c_str(), 0, &face);
			FT_Set_Pixel_Sizes(face, 0, 32);
			hb_font = hb_ft_font_create_referenced(face);
		}

		{ //alpha blending
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		{ //one buffer, reused for every quad this game ever draws:
			glGenVertexArrays(1, &text_vao);
			glGenBuffers(1, &text_vbo);
			glBindVertexArray(text_vao);
			glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
		}

		{ //VERTEX STRUCT TO OPENGL SHADER
			glVertexAttribPointer(
				color_texture_program->Position_vec4, //attribute
				2,									  //size
				GL_FLOAT,							  //type
				GL_FALSE,							  //normalized
				sizeof(Vert),						  //stride
				(GLbyte *)0 + offsetof(Vert, x)		  //offset
			);
			glEnableVertexAttribArray(color_texture_program->Position_vec4);

			glVertexAttribPointer(
				color_texture_program->Color_vec4, //attribute
				4,								   //size
				GL_UNSIGNED_BYTE,				   //type
				GL_TRUE,						   //normalized: 0..255 becomes 0.0..1.0
				sizeof(Vert),					   //stride
				(GLbyte *)0 + offsetof(Vert, col)  //offset
			);
			glEnableVertexAttribArray(color_texture_program->Color_vec4);

			glVertexAttribPointer(
				color_texture_program->TexCoord_vec2, //attribute
				2,									  //size
				GL_FLOAT,							  //type
				GL_FALSE,							  //normalized
				sizeof(Vert),						  //stride
				(GLbyte *)0 + offsetof(Vert, u)		  //offset
			);
			glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);
		}
	}

	hb_buffer_t *buf = hb_buffer_create();
	hb_buffer_add_utf8(buf, line.c_str(), -1, 0, -1);
	hb_buffer_guess_segment_properties(buf);
	hb_shape(hb_font, buf, nullptr, 0);

	unsigned n;
	hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &n);
	hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &n);

	for (unsigned i = 0; i < n; ++i)
		x -= pos[i].x_advance * scale / 128.0f;

	for (unsigned i = 0; i < n; ++i)
	{
		if (reveal >= 0 && int(i) >= reveal)
			break;
		Glyph const &g = glyph(info[i].codepoint);
		float gx = x + g.bx * scale, gy = y - (g.h - g.by) * scale;
		float gw = g.w * scale, gh = g.h * scale;
		Vert verts[6] = {
			{gx, gy, color, 0.0f, 1.0f},
			{gx + gw, gy, color, 1.0f, 1.0f},
			{gx + gw, gy + gh, color, 1.0f, 0.0f},
			{gx, gy, color, 0.0f, 1.0f},
			{gx + gw, gy + gh, color, 1.0f, 0.0f},
			{gx, gy + gh, color, 0.0f, 0.0f},
		};
		glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
		glBindTexture(GL_TEXTURE_2D, g.tex);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		x += pos[i].x_advance * scale / 64.0f;
	}
	hb_buffer_destroy(buf);
}

PlayMode::PlayMode()
{
	if (!bg_tex)
	{
		bg_tex = load_texture("sprites/3judges-bg.png");
		for (int j = 0; j < 3; ++j)
			for (int f = 0; f < FRAMES; ++f)
				judge_tex[j][f] = load_texture(judge_files[j][f]);
	}
	int start = rand() % 8;
	for (int i = 0; i < 4; ++i)
		shown[i] = (start + i) % 8;
	answers_len = 0;
	for (int i = 0; i < 4; ++i)
		answers_len = std::max(answers_len, int(strlen(names[shown[i]])));
}

void PlayMode::next_question()
{
	if (!music)
		music = Sound::loop(*bg_sample, 0.18f, 0.0f);
	do
		q = rand() % 23;
	while (used[q]);
	caught_line.clear();
	used[q] = true;
	asker = (questions[q].asker == 3) ? rand() % 3 : questions[q].asker;
	int start = rand() % questions[q].count;
	for (int i = 0; i < 4; ++i)
		shown[i] = (start + i) % questions[q].count;
	answers_len = 0;
	for (int i = 0; i < 4; ++i)
		answers_len = std::max(answers_len, int(strlen(questions[q].answers[shown[i]].text)));
	phase = ASKING;
	timer = 0.0f;
	last_glyph = 0;
}

void PlayMode::advance()
{
	if (burst > 0)
	{
		--burst;
		next_question();
	}
	else if (done < 6 && done < 23)
		next_question();
	else if (done < 23 && (done == 6 || rand() % 2))
		phase = CHOOSING;
	else
		phase = OVER;
	if (phase == OVER && !ending)
	{
		int lo = std::min(score[0], std::min(score[1], score[2]));
		float avg = float(score[0] + score[1] + score[2]) / float(done);
		if (contradictions >= 3)
			ending = E_PIG;
		else if (lo <= -6)
			ending = E_TARTARUS;
		else if (avg >= 2.2f && !contradictions && lo >= 3)
			ending = E_AIDE;
		else if (avg >= 1.5f && contradictions <= 1)
			ending = E_ELYSIUM;
		else if (avg >= 1.5f)
			ending = E_PRINCE;
		else if (avg >= 0.4f)
			ending = E_ASPHODEL;
		else
			ending = E_TARTARUS;
		start_ending();
	}
}

void PlayMode::start_ending()
{
	if (music)
		music->stop(1.0f);
	music = nullptr;
	phase = OVER;
	ending_i = 0;
	timer = 0.0f;
	last_glyph = 0;
	asker = endings[ending].lines[0].who;
}

PlayMode::~PlayMode()
{
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size)
{
	if (evt.type != SDL_EVENT_KEY_DOWN)
		return false;
	int k = evt.key.scancode - SDL_SCANCODE_1;
	if (phase == NAMING && k >= 0 && k < 4 && timer * glyphs_per_second >= answers_len)
	{
		Sound::play(*select_sample, 0.6f);
		name = names[shown[k]];
		asker = intro[0].who;
		phase = INTRO;
		intro_i = 0;
		timer = 0.0f;
	}
	else if (phase == ANSWERING && k >= 0 && k < 4 && timer * glyphs_per_second >= answers_len)
	{
		Sound::play(*select_sample, 0.6f);
		Answer const &a = questions[q].answers[shown[k]];
		for (int j = 0; j < 3; ++j)
		{
			float m = mood[j] > 0 ? 1.5f : (mood[j] < 0 ? 0.5f : 1.0f);
			score[j] += int(a.score[j] * (a.score[j] > 0 ? m : 2.0f - m));
			mood[j] = glm::clamp(mood[j] + a.mood[j], -3, 3);
			if (a.mood[j] < 0)
				judges[j].sideye = 2.0f;
		}
		++done;
		phase = REACTING;
		timer = 0.0f;
		if (a.ending)
		{
			ending = a.ending;
			start_ending();
			return true;
		}
		for (int c = 0; c < 2; ++c)
		{
			int f = a.field[c];
			if (f == F_NONE)
				continue;
			if (!claims[f])
				claims[f] = a.value[c];
			else if (c == 0 && strcmp(claims[f], a.value[c]))
			{
				++contradictions;
				score[1] -= 3;
				mood[1] = glm::clamp(mood[1] - 2, -3, 3);
				judges[1].sideye = 2.0f;
				caught_line = std::string("YOU SAID ") + claims[f] + ", NOW YOU SAY " + a.value[0] + " FOR " + field_names[f] + ".";
				stinger = 3;
				asker = 1;
				last_glyph = 0;
			}
			else if (c == 0)
				score[1] += 1;
		}
	}
	else if (phase == CHOOSING && k == 0)
	{
		burst = rand() % 4;
		next_question();
	}
	else if (phase == CHOOSING && k == 1)
	{
		Sound::play(*select_sample, 0.6f);
		phase = OVER;
		advance();
	}
	else if (phase == OVER && evt.key.scancode == SDL_SCANCODE_R)
		Mode::set_current(std::make_shared<PlayMode>());
	return true;
}

void PlayMode::update(float elapsed)
{
	text_time += elapsed;
	timer += elapsed;
	for (int j = 0; j < 3; ++j)
		judge_update(judges[j], elapsed);
	if (phase == INTRO && timer > strlen(intro[intro_i].text) / glyphs_per_second + 2.0f)
	{
		++intro_i;
		timer = 0.0f;
		last_glyph = 0;
		if (intro_i == intro_count)
			next_question();
		else
			asker = intro[intro_i].who;
	}
	else if (phase == ASKING && timer > strlen(questions[q].text) / glyphs_per_second + 0.5f)
	{
		phase = ANSWERING;
		timer = 0.0f;
	}
	else if (phase == REACTING && timer > (caught_line.empty() ? 2.0f : caught_line.size() / glyphs_per_second + 2.0f))
		advance();
	else if (phase == OVER && ending_i < endings[ending].count &&
			 timer > strlen(endings[ending].lines[ending_i].text) / glyphs_per_second + 2.0f)
	{
		++ending_i;
		timer = 0.0f;
		last_glyph = 0;
		if (ending_i < endings[ending].count)
			asker = endings[ending].lines[ending_i].who;
	}

	if (stinger > 0)
	{
		stinger_timer -= elapsed;
		if (stinger_timer <= 0.0f)
		{
			Sound::play(*select_sample, 0.6f);
			stinger_timer = 0.08f;
			--stinger;
		}
	}

	char const *typing = phase == NAMING ? "WHAT IS YOUR NAME, SHADE?"
						 : phase == INTRO ? intro[intro_i].text
						 : phase == ASKING ? questions[q].text
						 : (phase == REACTING && !caught_line.empty()) ? caught_line.c_str()
						 : (phase == OVER && ending_i < endings[ending].count) ? endings[ending].lines[ending_i].text
																			   : nullptr;
	talking = false;
	if (typing)
	{
		int len = int(strlen(typing));
		int g = std::min(int(timer * glyphs_per_second), len);
		talking = g < len;
		if (g > last_glyph)
		{
			if (g % 2 == 0)
				Sound::play(voice(asker), 0.5f);
			last_glyph = g;
		}
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size)
{
	glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	glm::mat4 to_clip = glm::ortho(0.0f, float(drawable_size.x), 0.0f, float(drawable_size.y));
	glUseProgram(color_texture_program->program);
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(to_clip));
	glBindVertexArray(text_vao);
	glBindBuffer(GL_ARRAY_BUFFER, text_vbo);

	float w = float(drawable_size.x), h = float(drawable_size.y);
	float sx = w / 360.0f, sy = h / 180.0f;
	glm::u8vec4 white(0xff, 0xff, 0xff, 0xff);

	bool hades_cut = phase == OVER && ending_i < endings[ending].count && endings[ending].lines[ending_i].who == 3;
	if (!hades_cut)
	{
		sprite_draw(bg_tex, 0.0f, 0.0f, w, h);
		for (int j = 0; j < 3; ++j)
			sprite_draw(judge_tex[j][judge_frame(j, asker, talking, text_time, ending)], 0.0f, 0.0f, w, h);
	}

	if (hades_cut)
	{
		std::vector<std::string> hlines = wrap(endings[ending].lines[ending_i].text, 12);
		int hleft = int(timer * glyphs_per_second);
		for (size_t i = 0; i < hlines.size(); ++i)
		{
			text_draw(hlines[i], 180.0f * sx, h - (60.0f + i * 24.0f) * sy, white, hleft, 4.0f);
			hleft = std::max(0, hleft - int(hlines[i].size()));
		}
		return;
	}

	if (phase == OVER && ending_i >= endings[ending].count)
	{
		text_draw(std::string(name) + ", " + ending_names[ending], 180.0f * sx, h - 164.0f * sy, white, -1);
		text_draw("ENDING " + std::to_string(ending) + " OF 9", 180.0f * sx, h - 170.0f * sy, white, -1);
		text_draw(std::to_string(score[0]) + " / " + std::to_string(score[1]) + " / " + std::to_string(score[2]) + "   LIES: " + std::to_string(contradictions), 180.0f * sx, h - 176.0f * sy, white, -1);
		text_draw("PRESS R", 180.0f * sx, h - 158.0f * sy, glm::u8vec4(0x00, 0x00, 0x00, 0xff), -1);
		return;
	}

	bool caught = phase == REACTING && !caught_line.empty();
	float qx = (asker == 0 ? 177.0f : asker == 1 ? 106.0f : 253.0f) * sx;
	int left = int(timer * glyphs_per_second);
	std::string prompt = caught ? "HOLD ON. YOU CONTRADICTED YOURSELF. YOU LIE! " + caught_line
						 : phase == NAMING ? "WHAT IS YOUR NAME, SHADE?"
						 : phase == INTRO  ? intro[intro_i].text
						 : phase == OVER   ? endings[ending].lines[ending_i].text
										   : questions[q].text;
	if (phase == ANSWERING || phase == CHOOSING || (phase == REACTING && !caught))
		left = -1;
	std::vector<std::string> qlines = wrap(prompt, 30);
	for (size_t i = 0; i < qlines.size(); ++i)
	{
		text_draw(qlines[i], qx, h - (34.0f + i * 6.0f) * sy, white, left);
		if (left >= 0)
			left = std::max(0, left - int(qlines[i].size()));
	}

	if (phase == CHOOSING)
	{
		text_draw("SPEAK ON, OR LET THE JUDGMENT END.", 180.0f * sx, h - 166.0f * sy, white, -1);
		text_draw("PRESS 1 TO SPEAK ON", 90.0f * sx, h - 174.0f * sy, white, -1);
		text_draw("PRESS 2 TO END THE TRIAL", 270.0f * sx, h - 174.0f * sy, white, -1);
		return;
	}

	if (caught)
		return;

	if (phase == ASKING || phase == INTRO || phase == OVER)
		return;

	for (int i = 0; i < 4; ++i)
	{
		float cx = (45.0f + i * 90.0f) * sx;
		std::vector<std::string> alines = wrap(phase == NAMING ? names[shown[i]] : questions[q].answers[shown[i]].text, 40);
		int aleft = (phase == ANSWERING || phase == NAMING) ? int(timer * glyphs_per_second) : -1;
		for (size_t l = 0; l < alines.size(); ++l)
		{
			text_draw(alines[l], cx, h - (166.0f + l * 6.0f) * sy, white, aleft);
			if (aleft >= 0)
				aleft = std::max(0, aleft - int(alines[l].size()));
		}
		if ((phase == ANSWERING || phase == NAMING) && timer * glyphs_per_second >= answers_len)
			text_draw("PRESS " + std::to_string(i + 1), cx, h - 160.0f * sy, glm::u8vec4(0x00, 0x00, 0x00, 0xff), -1);
	}
}

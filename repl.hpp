#include <iostream>
#include <termios.h> 
#include <unistd.h>
#include <map>
#include <functional>
#include <chrono>
//#include <stropts.h>
//#include <fcntl.h>
//#include <cctype>
//#include <memory>
///#include <optional>

#define ESC '\33'

class Repl
{
	//{{{ Internal state
	using milliseconds = std::chrono::duration<int64_t, std::milli>;
	using steady_clock = std::chrono::steady_clock;
	milliseconds escape_sequence_timeout;

	std::string prompt;
	std::string line;
	int curpos;

	std::vector<std::string> accepted_lines;
	std::vector<std::string> history;
	std::optional<std::function<void(std::string,int)>> word_completer;
	std::optional<std::function<void(std::string,int)>> multi_completer;
	std::optional<std::function<void(std::vector<std::string>,std::string,int)>> hist_completer;
	std::optional<std::function<void(std::vector<std::string>,std::string,int)>> global_hist_fwd;
	std::optional<std::function<void(std::vector<std::string>,std::string,int)>> global_hist_bwd;
	std::optional<std::function<void(std::vector<std::string>,std::string,int)>> matching_hist_fwd;
	std::optional<std::function<void(std::vector<std::string>,std::string,int)>> matching_hist_bwd;

	//{{{
	enum Mode
	{
		INSERT         = 0, // Default: typed keys are appended to the command line
		NORMAL         = 1, // After pressing ESC: keys are interpreted as commands
		INSERT_ESCAPED = 2, // Insert mode while processing an escape sequence
		NORMAL_ESCAPED = 3	// Normal mode while processing an escape sequence
	} mode;
	//}}}
	//}}}

	//{{{
	void change_terminal_mode(int dir)
	{
		//source: https://stackoverflow.com/a/13129698/4360539
		static struct termios oldt, newt;
		//static long   flags;

		if ( dir == 1 )
		{
			tcgetattr( STDIN_FILENO, &oldt);
			newt = oldt;
			newt.c_lflag &= ~( ICANON | ECHO );
			tcsetattr( STDIN_FILENO, TCSANOW, &newt);
			//flags = fcntl(STDIN_FILENO, F_GETFL);
			//fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
		}
		else
		{
			tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
			//fcntl(STDIN_FILENO, F_SETFL, flags);
		}
	}
	//}}}

	//{{{
	inline void check_curpos(void)
	{
		if(curpos < 0) curpos=0;
		if(static_cast<int>(line.length()) < curpos) curpos = line.length();
	}
	//}}}

	//{{{ On keypress functions

	using on_keypress_function = std::function<bool(char)>;
	using Key = char;
	using action_map = std::map<std::string, on_keypress_function>;

	// All the actions a key press can trigger. Yes, this is one huge std::map of lambdas. C++ does not offer reflection and this way there is only one definition.
	std::map<std::string, on_keypress_function> actions = { // TODO: Replace with normal functions when reflection becomes available in C++
		//{{{ Cursor movement

		{ "move_cursor_begin",        [this](char  ){ curpos=0; return false; }},
		{ "move_cursor_left",         [this](char  ){ if(curpos) curpos--; return false; }},
		{ "move_cursor_right",        [this](char  ){ if(curpos<static_cast<int>(line.length())) curpos++; return false; }},
		{ "move_cursor_end",          [this](char  ){ curpos=line.length(); return false; }},
		{ "move_cursor_forward_word", [this](char  ){ for(; curpos<static_cast<int>(line.length())&&line[curpos]!=' '; curpos++); return false; }},
		{ "move_cursor_backward_word",[this](char  ){ for(; curpos&&line[curpos]!=' '; curpos--); return false; }},
		//}}}
		//{{{ Mode change

		{ "changemode_insert",        [this](char  ){ mode=INSERT; return false; }},
		{ "changemode_append",        [this](char  ){ curpos++; mode=INSERT; return false; }}, // TODO: Check
		{ "changemode_normal",        [this](char  ){ mode=NORMAL; return false; }},
		//}}}
		//{{{ Insert/delete character
   
		{ "add_char",                 [this](char c){ line.insert(curpos,1,c); curpos++; return false; }},
		{ "delete_char",              [this](char  ){ line.erase(curpos,1); curpos++; return false; }},
		//}}}
		//{{{ Accept line

		{ "accept",                   [this](char  ){ curpos=0; history.push_back(line); accepted_lines.push_back(line); line.clear(); return true; }},
		{ "accept_no_add_history",    [this](char  ){ curpos=0; accepted_lines.push_back(line); line.clear(); return true; }},
		//}}}
		//{{{ Complete

		{ "complete_single_word",     [this](char  ){ if(word_completer)   std::invoke(*word_completer,      line, curpos); check_curpos(); return false; }},
		{ "complete_multi_word",      [this](char  ){ if(multi_completer)  std::invoke(*multi_completer,     line, curpos); check_curpos(); return false; }},
		{ "complete_history",         [this](char  ){ if(hist_completer)   std::invoke(*hist_completer,history,line,curpos); check_curpos(); return false;}},
		//}}}
		//{{{ History search

		{ "search_global_hist_fwd",   [this](char  ){ if(global_hist_fwd)  std::invoke(*global_hist_fwd,  history,line,curpos); check_curpos(); return false; }},
		{ "search_global_hist_bwd",   [this](char  ){ if(global_hist_fwd)  std::invoke(*global_hist_fwd,  history,line,curpos); check_curpos(); return false; }},
		{ "search_matching_hist_fwd", [this](char  ){ if(matching_hist_fwd)std::invoke(*matching_hist_fwd,history,line,curpos); check_curpos(); return false; }},
		{ "search_matching_hist_bwd", [this](char  ){ if(matching_hist_bwd)std::invoke(*matching_hist_bwd,history,line,curpos); check_curpos(); return false; }},
		//}}}
	};

	std::array<action_map, 4> mappings; // Mappings for all 4 modes (INSERT, NORMAL, INSERT_ESCAPED, NORMAL_ESCAPED)
	//}}}

	//{{{ Insert one key press or escape sequence

	void insert_key(Key key)
	{
		insert_key(std::string(1, key));
	}
	bool insert_key(std::string key)
	{
		static on_keypress_function* add_char=&actions.find("add_char")->second;

		// If there is a mapping, execute the mapped function...
		if(auto fun = mappings[mode].find(key); fun != mappings[mode].end())
		{
			std::invoke(fun->second, key[0]);
			return true;
		}
		else if(mode==INSERT && std::isprint(key[0])) // ... if not, add to the line if in insert mode
		{
			std::invoke(*add_char, key[0]);
		}
		else if(mode==INSERT_ESCAPED || mode==NORMAL_ESCAPED) // ... if not, print some debugging information
		{
			key.erase(0, 1);
			std::cerr<<"Unknown escape sequence ESC"<<key<<std::endl;
		}
		return false;
	}
	//}}}


	public:
	//{{{
	void draw(void)
	{
		int cursor_position = prompt.length() + curpos;
		std::cout<<"\33[2K\r"<<prompt<<line<<std::flush;
		std::cout<<"\33["<<cursor_position<<"G"<<std::flush;
	}
	//}}}

	//{{{
	void map(Mode mode, std::string key_combo, std::string action)
	{
		// Escape sequences are passed as 'E''S''C''['... , turn this into 'ESC''['... (\33[...)
		if(key_combo.length() >= 3 && key_combo.find("ESC")==0)
		{
			key_combo.erase(0, 2);
			key_combo[0] = ESC;
		}
		if(auto fun = actions.find(action); fun != actions.end())
		{
			mappings[mode][key_combo] = fun->second;
		}
		else
		{
			if(key_combo[0] != ESC)
			{
				std::cerr<<"Cannot map key combo \""<<key_combo<<"\" --> action \""<<action<<"\": Action unknown."<<std::endl;
			}
			else
			{
				key_combo.erase(0, 1);
				std::cerr<<"Cannot map escape sequece \"ESC"<<key_combo<<"\" --> action \""<<action<<"\": Action unknown."<<std::endl;
			}
		}
	}
	//}}}

	//{{{ Handle user input

	// Handle user input one key at a time
	// Depending on mode (NORMAL/INSERT) key mappings are checked and corresponding functions are run.
	// It may be that the input is not a key the user pressed but an escape sequence sent by the terminal,
	// and there are two ways to check this. They both use the fact, that a user is slow, while the terminal
	// is fast an can send multiple 'keys' at once.
	// Option 1) When an ESC is entered, start a timer and store the key in a special buffer. If there is
	// input before the timer runs out (and that input is "[..."), the input must have been an escape sequence
	// and should be treated as such. If there was no follow-up input, it will have been a literal ESC pressed by the user.
	// Option 2) Look at multiple keys at once: If the terminal is configured to send keys immediately, we should normally only receive single keys, but if we receive an escape sequence, we will receive multiple keys at once and so should be able to just check if the next key is '['.

	int insert(char buf[], std::size_t len)
	{
		static std::string escape_sequence_buffer;
		steady_clock::time_point sequence_start;
		for(std::size_t i=0; i<len; i++)
		{
			//std::cout<<int(buf[i])<<std::endl;

			//{{{
			if(mode != INSERT_ESCAPED && mode != NORMAL_ESCAPED) // If we are not in an escape sequence
			{
				switch(buf[i])
				{
					// Other special caases
					case ESC:
						// see if we can get the next character
						if(i+1 >= len) // If we cannot get the next character we'll have to set a timeout to know when/if it arrives
						{
							escape_sequence_buffer+=ESC;
							sequence_start=steady_clock::now();
							if(mode==INSERT) mode=INSERT_ESCAPED;
							if(mode==NORMAL) mode=NORMAL_ESCAPED;
							// Start a timeout: When it completes, it will call insert(ESC, 1)
							// This way, we know that this was a real key press
							// TODO: start the timer
							break;
						}
						else //if(i+1 < len)
						{
							if(buf[i+1] == '[') // This IS the start of an escape sequence
							{
								i++; // we consume the (i+1)th key as well so hop over it in the next iteration.
								escape_sequence_buffer.append("\33[");
								sequence_start=steady_clock::now();
								if(mode==INSERT) mode=INSERT_ESCAPED;
								if(mode==NORMAL) mode=NORMAL_ESCAPED;
							}
							//else // This is NOT the start of an escape sequence so process the key press (using the fallthrough)
						}
						[[fallthrough]];
					default:
						insert_key(buf[i]);
				}
			}
			//}}}
			//{{{
			else // mode == INSERT_ESCAPED || mode==NORMAL_ESCAPED means we are possibly in an escape sequence
			{
				if(escape_sequence_buffer.length() == 1)
				{
					// If the timer is running, stop it
					// TODO: Stop the timer

					// Escape sequences begin with "ESC[" and the '[' should arrive faster than the user can type
					if( (buf[i] == '[') && (std::chrono::duration_cast<milliseconds>(sequence_start-steady_clock::now()) < escape_sequence_timeout))
					{
						// This is an escape sequence, the next characters determine what action should be taken
						escape_sequence_buffer+=buf[i];
					}
					else
					{
						// This is not an escape sequence, so treat the stored 'ESC' and buf[i] as keys
						if(mode==INSERT_ESCAPED) mode=INSERT;
						if(mode==NORMAL_ESCAPED) mode=NORMAL;
						insert_key(ESC);
						escape_sequence_buffer.clear();
						if(buf[i] != ESC) // If we were called by the timer, the second key is inserted by the timer and should be ignored
						{
							insert_key(buf[i]);
						}
					}
				}
				else
				{
					// This is an escape sequence, add the new character and try to run it.
					escape_sequence_buffer+=buf[i];
					if(insert_key(escape_sequence_buffer))
					{
						// The escape sequence was recognised, so we are not in an escape sequence any more.
						escape_sequence_buffer.clear();
						if(mode==INSERT_ESCAPED) mode=INSERT;
						if(mode==NORMAL_ESCAPED) mode=NORMAL;
					}
				}
			}
			//}}}
		}
		draw();
		return accepted_lines.size();
	}
	//}}}

	//{{{
	std::vector<std::string>& get_accepted_lines(void)
	{
		return accepted_lines;
	}
	//}}}

	//{{{
	Repl(const std::string& prompt = "edit > ", milliseconds escape_sequence_timeout = milliseconds(100)) :
		escape_sequence_timeout(escape_sequence_timeout),
		prompt(prompt),
		line(""),
		curpos(0),
		accepted_lines(),
		history(),
		mode(Mode::INSERT)
	{
		change_terminal_mode(1);
		draw();
	}

	~Repl(void)
	{
		change_terminal_mode(0);
	}
	//}}}
};

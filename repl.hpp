#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <map>
#include <functional>
#include <chrono>
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

#define ESC '\33'
#define DEL '\177'
#define ETX '\03'
#define EOT '\04'
#define KILL_PILL "kill_pill"

class Repl
{
	using milliseconds = std::chrono::duration<int64_t, std::milli>;
	using steady_clock = std::chrono::steady_clock;

	//{{{ Internal state
	milliseconds escape_sequence_timeout;
	bool set_terminal_mode;
	fs::path histfile;

	std::string prompt;
	//{{{
	class Curpos
	{
		const Repl& repl;
		int position;
		//{{{
		void check(void)
		{
			if((repl.mode==INSERT || repl.mode==INSERT_ESCAPED) && (static_cast<int>(repl.history[repl.hist_idx].length()) <= position)) position = repl.history[repl.hist_idx].length();
			if((repl.mode==NORMAL || repl.mode==NORMAL_ESCAPED) && (static_cast<int>(repl.history[repl.hist_idx].length()) <= position)) position = repl.history[repl.hist_idx].length()-1;
			if(position <= 0) position=0;
		}
		//}}}
		public:
		//{{{ Modifiers

		Curpos(const Repl& repl, int position=0) : repl(repl), position(position)
		{
			check();
		}

		Curpos(const Curpos& other) : repl(other.repl), position(other.position) {}




		Curpos& operator=(int new_position) 
		{
			position=new_position;
			check();
			return *this;
		}
		const Curpos operator+(int rhs)
		{
			Curpos tmp(*this);
			tmp += rhs;
			return tmp;
		}
		const Curpos operator-(int rhs)
		{
			Curpos tmp(*this);
			tmp -= rhs;
			return tmp;
		}
		Curpos& operator+=(int rhs)
		{
			position+=rhs;
			check();
			return *this;
		}
		Curpos& operator-=(int rhs)
		{
			position-=rhs;
			check();
			return *this;
		}
		Curpos& operator++() //Praeinkrement
		{
			position++;
			check();
			return *this;
		}
		const Curpos operator++(int) //Postinkrement	
		{
			Curpos tmp(*this);
			++(*this);
			return tmp;
		}
		Curpos& operator--() //Praeinkrement
		{
			position--;
			check();
			return *this;
		}
		const Curpos operator--(int) //Postinkrement	
		{
			Curpos tmp(*this);
			--(*this);
			return tmp;
		}
		bool operator==(int rhs) const
		{
			return position==rhs;
		}
		bool operator!=(int rhs) const
		{
			return position!=rhs;
		}
		bool operator<=(int rhs) const
		{
			return position<=rhs;
		}
		bool operator>=(int rhs) const
		{
			return position>=rhs;
		}
		bool operator<(int rhs) const
		{
			return position<rhs;
		}
		bool operator>(int rhs) const
		{
			return position>rhs;
		}

		operator bool(void) const
		{
			return position;
		}
		operator int(void) const
		{
			return position;
		}
		operator long unsigned int(void) const
		{
			return static_cast<long unsigned int>(position);
		}
		//}}}
	} curpos;
	//}}}

	std::vector<std::string> accepted_lines;
	std::vector<std::string> history;
	std::size_t hist_idx;

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

	//{{{ On keypress functions

	using on_keypress_function = std::function<bool(char)>;
	using Key = char;
	using action_map = std::map<std::string, on_keypress_function>;

	// All the actions a key press can trigger. Yes, this is one huge std::map of lambdas. C++ does not offer reflection and this way there is only one definition.
	action_map all_actions = { // TODO: Replace with normal functions when reflection becomes available in C++
		//{{{ Cursor movement

		{ "move_cursor_begin",        [this](Key  ){ curpos=0; return false; }},
		{ "move_cursor_left",         [this](Key  ){ curpos--; return false; }},
		{ "move_cursor_right",        [this](Key  ){ curpos++; return false; }},
		{ "move_cursor_end",          [this](Key  ){ curpos=history[hist_idx].length(); return false; }},
		{ "move_cursor_word_begin",   [this](Key  ){ for(; curpos&&history[hist_idx][curpos]!=' '; --curpos); return false; }}, // TODO
		{ "move_cursor_word_end",     [this](Key  ){ for(; curpos<static_cast<int>(history[hist_idx].length())&&history[hist_idx][curpos]!=' '; ++curpos); return false; }}, // TODO
		{ "move_cursor_word_next",    [this](Key  ){ for(; curpos<static_cast<int>(history[hist_idx].length())&&history[hist_idx][curpos]!=' '; ++curpos); return false; }}, // TODO
		//}}}
		//{{{ Mode change

		{ "changemode_insert_begin",  [this](Key  ){ mode=INSERT; curpos=0; return false; }}, // TODO: Check
		{ "changemode_insert",        [this](Key  ){ mode=INSERT; return false; }},
		{ "changemode_append",        [this](Key  ){ mode=INSERT; curpos++; return false; }}, // TODO: Check
		{ "changemode_append_end",    [this](Key  ){ mode=INSERT; curpos=history[hist_idx].length(); return false; }}, // TODO: Check
		{ "changemode_normal",        [this](Key  ){ mode=NORMAL; curpos--; return false; }},
		//}}}
		//{{{ Insert/delete character
 
		{ "add_char",                 [this](Key c){ history[hist_idx].insert(curpos,1,c); curpos++; return false; }},
		{ "delete_char",              [this](Key  ){ if(history[hist_idx].length()) history[hist_idx].erase(curpos,1); return false; }},
		{ "backspace",                [this](Key  ){ if(history[hist_idx].length()) history[hist_idx].erase(--curpos,1); return false; }},
		//}}}
		//{{{ Accept/kill line

		{ "accept",                   [this](Key  ){
				curpos=0;
				accepted_lines.push_back(history[hist_idx]);
				if(hist_idx == history.size()-1) history.push_back("");
				hist_idx = history.size()-1;
				history[hist_idx].clear();
				mode=INSERT;
				return true;
		}},
		{ "accept_no_add_history",    [this](Key  ){
				curpos=0;
				accepted_lines.push_back(history[hist_idx]);
				hist_idx = history.size()-1;
				history[hist_idx].clear();
				mode=INSERT;
				return true;
		}},
		{ "kill_line",                [this](Key  ){
				curpos=0;
				hist_idx = history.size()-1;
				history[hist_idx].clear();
				mode=INSERT;
				return true;
		}},
		{ "kill_repl",                [this](Key  ){ accepted_lines.push_back(KILL_PILL); return true; }},
		//}}}
		//{{{ Complete

		{ "complete_single_word",     [this](Key  ){ if(word_completer)   std::invoke(*word_completer,      history[hist_idx], curpos); return false; }},
		{ "complete_multi_word",      [this](Key  ){ if(multi_completer)  std::invoke(*multi_completer,     history[hist_idx], curpos); return false; }},
		{ "complete_history",         [this](Key  ){ if(hist_completer)   std::invoke(*hist_completer,history,history[hist_idx],curpos); return false;}},
		//}}}
		//{{{ History search

		{ "print_history",            [this](Key  ){ std::cout<<'\n'; for(auto& s : history) std::cout<<'\t'<<s<<std::endl; return false;}},
		
		{ "search_global_hist_fwd",   [this](Key  ){ 
				hist_idx = (hist_idx > 0)                ? hist_idx-1 : hist_idx;
				curpos=history[hist_idx].length();
				return false; }},
		{ "search_global_hist_bwd",   [this](Key  ){ 
				hist_idx = (hist_idx < history.size()-1) ? hist_idx+1 : hist_idx;
				curpos=history[hist_idx].length();
				return false; }},
		{ "search_matching_hist_fwd", [this](Key  ){ if(matching_hist_fwd)std::invoke(*matching_hist_fwd,history,history[hist_idx],curpos); return false; }},
		{ "search_matching_hist_bwd", [this](Key  ){ if(matching_hist_bwd)std::invoke(*matching_hist_bwd,history,history[hist_idx],curpos); return false; }},
		//}}}
	};

	std::array<action_map, 4> mappings; // Mappings for all 4 modes (INSERT, NORMAL, INSERT_ESCAPED, NORMAL_ESCAPED)
	//}}}

	void insert_key(Key key);
	bool insert_key(std::string key);

	public:
	//{{{

	std::optional<std::function<void(std::string,int)>> word_completer;                                     //         line, cursor position
	std::optional<std::function<void(std::string,int)>> multi_completer;                                    //         line, cursor position
	std::optional<std::function<void(const std::vector<std::string>&,std::string&,int)>> hist_completer;    //History, line, cursor position

	std::optional<std::function<void(const std::vector<std::string>&,std::string&,int)>> matching_hist_fwd; //History, line, cursor position
	std::optional<std::function<void(const std::vector<std::string>&,std::string&,int)>> matching_hist_bwd; //History, line, cursor position
	//}}}
	void change_terminal_mode(int dir); // Set the terminal to unbuffered mode on begin, reset to buffered on end. With libuv use libuv's means instead;
	void draw(void);

	void map(Mode mode, Key key_combo, std::string action);
	void map(Mode mode, std::string key_combo, std::string action);

	void default_mappings(void);

	std::vector<std::string> insert(char buf[], std::size_t len);

	Repl(fs::path histfile = "", const std::string& prompt = "edit > ", milliseconds escape_sequence_timeout = milliseconds(100), bool set_terminal_mode = false);
	~Repl(void);
};

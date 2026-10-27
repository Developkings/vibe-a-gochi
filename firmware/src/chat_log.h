// Messages shown on the Chat screen. Filled from Hardware Buddy "turn" events (Claude app) and
// from the companion (Claude Code chats); the device's own quick replies are added as "mine".
#pragma once
#include <stdint.h>

enum class ChatKind : uint8_t { CLAUDE, MINE, TOOL };

struct ChatMsg {
  ChatKind kind;
  char text[1024];   // a long reply is cut at a whole UTF-8 character
};

constexpr int CHAT_MAX = 48;   // long replies arrive in several parts

void chat_clear();
void chat_add(ChatKind kind, const char *text);
int chat_count();
const ChatMsg &chat_at(int i);   // 0 = oldest kept
uint16_t chat_gen();             // bumps on every change (the screen rebuilds when it differs)

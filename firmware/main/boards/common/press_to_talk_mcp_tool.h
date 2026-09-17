#ifndef PRESS_TO_TALK_MCP_TOOL_H
#define PRESS_TO_TALK_MCP_TOOL_H

#include "mcp_server.h"
#include "settings.h"

// Reusable MCP tool for the press-to-talk button mode
class PressToTalkMcpTool {
private:
    bool press_to_talk_enabled_;

public:
    PressToTalkMcpTool();
    
    // initialise the tool and register it with the MCP server
    void Initialize();
    
    // get the current press-to-talk mode
    bool IsPressToTalkEnabled() const;

private:
    // MCP tool callback
    ReturnValue HandleSetPressToTalk(const PropertyList& properties);
    
    // internal: set the press-to-talk state and save it to settings
    void SetPressToTalkEnabled(bool enabled);
};

#endif // PRESS_TO_TALK_MCP_TOOL_H 
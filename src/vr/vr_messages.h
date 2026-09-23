#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace snap::vr {
// Snap mixes ASCII, full-width Latin glyphs, and two-byte button escapes.
inline std::string messageText(std::string_view source) {
    std::string out;
    for(size_t i=0;i<source.size();i++) {
        unsigned char c=source[i];
        if(c=='\\'&&i+1<source.size()) {
            char code=source[++i];
            if(code=='a')out+="TRIGGER";
            else if(code=='z')out+="GRIP";
            else if(code=='b')out+="B/Y";
            else if(code=='r')out+="B/Y";
            else if(code=='F')out+="NEXT";
            else if(code=='[')out+="NEW";
            else if(code=='#'&&i+1<source.size()) {
                char parameter=source[++i];i+=parameter=='X'||parameter=='Y'?2:1;
            }
        }else if(c==0xa3&&i+1<source.size())out+=char(static_cast<unsigned char>(source[++i])-0x80);
        else if(c>=0x80&&i+1<source.size()) {
            unsigned char second=source[++i];
            if(c==0xa5&&second==0xe5)out+='e';
            else out+=' ';
        }else out+=char(c);
    }
    if(out.find("Press GRIP to aim")!=std::string::npos)return "GRIP THE CAMERA TO AIM.";
    if(out.find("Press TRIGGER to shoot")!=std::string::npos)return "PULL THE CAMERA TRIGGER TO SHOOT.";
    if(out.find("Control Stick")!=std::string::npos)return "TURN YOUR HEAD OR AIM THE CAMERA TO LOOK AROUND.";
    return out;
}
inline std::vector<std::string> messageLines(std::string_view text,size_t width=42) {
    std::vector<std::string> lines;
    while(!text.empty()) {
        size_t count=std::min(width,text.size()),newline=text.find('\n');
        if(newline<=count)count=newline;
        else if(count<text.size()) {
            auto space=text.rfind(' ',count);if(space!=std::string_view::npos&&space>0)count=space;
        }
        lines.emplace_back(text.substr(0,count));text.remove_prefix(count);
        while(!text.empty()&&(text.front()==' '||text.front()=='\n'))text.remove_prefix(1);
    }
    return lines;
}
}

#pragma once
#include "client.hpp"
#include "module_abi.h"
#include <cctype>
#include <tuple>

namespace gweb::ui {
constexpr long long success_emoji=5774022692642492953LL;
constexpr long long failure_emoji=5774077015388852135LL;
constexpr long long clear_emoji=5845945815549350824LL;

struct Entity {
    int type=0,offset=0,length=0;
    long long document_id=0;
    std::string url;
};
struct Text {
    std::string text;
    std::vector<Entity> entities;
};
inline size_t character_bytes(std::string_view s,size_t at) {
    unsigned char c=static_cast<unsigned char>(s[at]);
    size_t n=c<128?1:c<224?2:c<240?3:4;
    if (at+n>s.size()) throw Error("text","Повреждённый UTF-8 в ответе.");
    return n;
}
inline int units(std::string_view text) {
    int count=0;
    for (size_t i=0;i<text.size();) {auto n=character_bytes(text,i);count+=n==4?2:1;i+=n;}
    return count;
}
inline bool space(char c) {return std::isspace(static_cast<unsigned char>(c));}
inline bool word(char c) {return static_cast<unsigned char>(c)>=128 || std::isalnum(static_cast<unsigned char>(c)) || c=='_';}
inline bool escaped(std::string_view text,size_t at) {
    size_t n=0;while (at>0 && text[--at]=='\\') ++n;return n%2;
}
inline size_t marker_end(std::string_view text,std::string_view marker,size_t start) {
    for(size_t at=start;at<text.size();) {
        if(text[at]=='\\'){at+=std::min<size_t>(2,text.size()-at);continue;}
        if(text[at]=='`') {
            size_t count=1;while(at+count<text.size() && text[at+count]=='`')++count;
            auto end=text.find(std::string(count,'`'),at+count);
            if(end!=std::string_view::npos){at=end+count;continue;}
            at+=count;continue;
        }
        if(text.substr(at,marker.size())==marker) {
            size_t count=0;while(at+count<text.size() && text[at+count]==marker[0])++count;
            size_t candidate=at;
            if(count!=marker.size()) {
                if(count==3 && marker.size()<3) candidate=at+count-marker.size();
                else {at+=count;continue;}
            }
            if(candidate>start && !space(text[candidate-1]))return candidate;
            at+=std::max(count,marker.size());continue;
        }
        ++at;
    }
    return std::string_view::npos;
}
inline void trim_entities(Text& value) {
    for(auto& e:value.entities) {
        int count=0;size_t byte=0;
        while(byte<value.text.size() && count<e.offset+e.length) {
            auto n=character_bytes(value.text,byte);count+=n==4?2:1;byte+=n;
        }
        while(e.length>0 && byte>0 && space(value.text[byte-1])) {--byte;--e.length;}
    }
    std::erase_if(value.entities,[](const Entity& e){return e.length<=0;});
}

class Markdown {
public:
    Text parse(std::string_view source) {
        size_t at=0;
        while (at<source.size()) {
            size_t end=source.find('\n',at);if (end==std::string_view::npos) end=source.size();
            auto line=source.substr(at,end-at);
            size_t indent=0;while (indent<line.size() && line[indent]==' ') ++indent;
            size_t fence=indent;
            if (indent<=3 && fence<line.size() && (line[fence]=='`' || line[fence]=='~')) {
                char marker=line[fence];size_t count=0;
                while (fence+count<line.size() && line[fence+count]==marker) ++count;
                if (count>=3 && end<source.size()) {
                    size_t body=end+1,closing=source.size(),after=source.size();
                    for (size_t cursor=body;cursor<source.size();) {
                        auto e=source.find('\n',cursor);if(e==std::string_view::npos)e=source.size();
                        auto row=source.substr(cursor,e-cursor);size_t i=0;
                        while(i<row.size() && row[i]==' ' && i<4)++i;
                        size_t begin=i;while(i<row.size() && row[i]==marker)++i;
                        if (begin<=3 && i-begin>=count && trim(row.substr(i)).empty()) {
                            closing=cursor;after=e<source.size()?e+1:e;break;
                        }
                        cursor=e<source.size()?e+1:e;
                    }
                    int begin=position_;append(source.substr(body,closing-body));
                    add(KOTO_ENT_PRE,begin,position_-begin);
                    at=after;continue;
                }
            }
            // Block syntax is handled outside inline parsing. Code is never
            // interpreted as Markdown or rewritten with premium emoji.
            size_t begin=indent;bool quote=false,heading=false;
            if (indent<=3) {
                while (begin<line.size() && line[begin]=='>') {
                    quote=true;++begin;if(begin<line.size() && line[begin]==' ')++begin;
                }
            }
            append(line.substr(0,indent));
            int block_start=position_;
            size_t hashes=begin;while(hashes<line.size() && line[hashes]=='#')++hashes;
            if (hashes>begin && hashes-begin<=6 && hashes<line.size() && space(line[hashes])) {
                heading=true;begin=hashes;while(begin<line.size() && space(line[begin]))++begin;
            }
            auto content=line.substr(begin);
            if (heading) {
                size_t tail=content.size();while(tail && space(content[tail-1]))--tail;
                size_t marks=tail;while(marks && content[marks-1]=='#')--marks;
                if (marks<tail && marks && space(content[marks-1])) content=content.substr(0,marks-1);
            }
            bool rule=false;
            if (!content.empty() && (content[0]=='-' || content[0]=='*' || content[0]=='_')) {
                size_t count=0;rule=true;
                for(char c:content) {if(c==content[0])++count;else if(!space(c)){rule=false;break;}}
                rule=rule && count>=3;
            }
            if (rule) append("────────");
            else {
                if (content.size()>1 && (content[0]=='*' || content[0]=='-' || content[0]=='+') && space(content[1])) {
                    append("• ");content.remove_prefix(2);
                }
                inline_text(content,0);
            }
            if (heading) add(KOTO_ENT_BOLD,block_start,position_-block_start);
            if (quote) add(KOTO_ENT_BLOCKQUOTE,block_start,position_-block_start);
            if(end<source.size()) append("\n");
            at=end<source.size()?end+1:end;
        }
        normalize();return std::move(result_);
    }
private:
    Text result_;int position_=0;
    void append(std::string_view text) {result_.text.append(text);position_+=units(text);}
    void add(int type,int offset,int length,std::string url={}) {
        if (length>0) result_.entities.push_back({type,offset,length,0,std::move(url)});
    }
    static bool safe_url(const std::string& url) {
        auto value=lower(url);
        return url.find_first_of("\r\n\t ")==std::string::npos &&
            (value.rfind("https://",0)==0 || value.rfind("http://",0)==0 || value.rfind("mailto:",0)==0 || value.rfind("tg://",0)==0);
    }
    bool link(std::string_view text,size_t& at,unsigned depth) {
        bool image=text[at]=='!';size_t open=at+(image?1:0);
        if (open>=text.size() || text[open]!='[') return false;
        size_t closing=open+1;int brackets=1;
        for (;closing<text.size();++closing) {
            if(escaped(text,closing))continue;
            if(text[closing]=='[' && ++brackets>16)return false;
            if(text[closing]==']' && --brackets==0)break;
        }
        if(closing+1>=text.size() || text[closing+1]!='(')return false;
        size_t end=closing+2;int parens=1;
        for(;end<text.size();++end) {
            if(escaped(text,end))continue;
            if(text[end]=='(' && ++parens>16)return false;
            if(text[end]==')' && --parens==0)break;
        }
        if(end==text.size())return false;
        std::string url=trim(text.substr(closing+2,end-closing-2));
        if(!url.empty() && url.front()=='<') {
            auto last=url.find('>');if(last==std::string::npos)return false;url=url.substr(1,last-1);
        } else {
            auto title=url.find_first_of(" \t");if(title!=std::string::npos)url.resize(title);
        }
        if(!safe_url(url))return false;
        int begin=position_;
        auto label=text.substr(open+1,closing-open-1);
        if(label.empty())append(url);else inline_text(label,depth+1,false);
        add(KOTO_ENT_URL,begin,position_-begin,std::move(url));at=end+1;return true;
    }
    void inline_text(std::string_view text,unsigned depth,bool links=true) {
        if(depth>=16){append(text);return;}
        for(size_t i=0;i<text.size();) {
            if(text[i]=='\\' && i+1<text.size() && std::ispunct(static_cast<unsigned char>(text[i+1]))) {
                append(text.substr(i+1,1));i+=2;continue;
            }
            if(text[i]=='`') {
                size_t ticks=1;while(i+ticks<text.size() && text[i+ticks]=='`')++ticks;
                std::string marker(ticks,'`');size_t end=text.find(marker,i+ticks);
                while(end!=std::string_view::npos && end+ticks<text.size() && text[end+ticks]=='`') {
                    size_t after=end+ticks;while(after<text.size() && text[after]=='`')++after;
                    end=text.find(marker,after);
                }
                if(end!=std::string_view::npos) {
                    auto code=text.substr(i+ticks,end-i-ticks);
                    if(code.size()>1 && code.front()==' ' && code.back()==' ' && code.find_first_not_of(' ')!=std::string_view::npos)
                        code=code.substr(1,code.size()-2);
                    int begin=position_;append(code);add(KOTO_ENT_CODE,begin,position_-begin);i=end+ticks;continue;
                }
            }
            if(links && (text[i]=='[' || (text[i]=='!' && i+1<text.size() && text[i+1]=='[')) && link(text,i,depth))continue;
            std::string_view marker;int type=0;bool triple=false;
            for(auto entry:{std::pair{"***",KOTO_ENT_BOLD}, {"___",KOTO_ENT_BOLD}, {"**",KOTO_ENT_BOLD}, {"__",KOTO_ENT_BOLD},
                            {"~~",KOTO_ENT_STRIKE}, {"||",KOTO_ENT_SPOILER}, {"*",KOTO_ENT_ITALIC}, {"_",KOTO_ENT_ITALIC}}) {
                auto token=std::string_view(entry.first);
                if(text.substr(i,token.size())!=token || i+token.size()>=text.size() || space(text[i+token.size()]))continue;
                if(token[0]=='_' && i>0 && word(text[i-1]))continue;
                marker=token;type=entry.second;triple=token.size()==3;break;
            }
            if(type) {
                size_t end=marker_end(text,marker,i+marker.size());
                if(end!=std::string_view::npos && !(marker[0]=='_' && end+marker.size()<text.size() && word(text[end+marker.size()]))) {
                    int begin=position_;inline_text(text.substr(i+marker.size(),end-i-marker.size()),depth+1,links);
                    add(type,begin,position_-begin);if(triple)add(KOTO_ENT_ITALIC,begin,position_-begin);
                    i=end+marker.size();continue;
                }
            }
            auto n=character_bytes(text,i);append(text.substr(i,n));i+=n;
        }
    }
    void normalize() {
        // Code/pre never overlap decoration. Split an outer emphasis or
        // heading around its inline code instead of creating invalid nesting.
        std::vector<Entity> code,decorated;
        for(const auto& e:result_.entities) if(e.type==KOTO_ENT_CODE || e.type==KOTO_ENT_PRE)code.push_back(e);
        for(const auto& e:result_.entities) {
            if(e.type==KOTO_ENT_CODE || e.type==KOTO_ENT_PRE){decorated.push_back(e);continue;}
            std::vector<Entity> pieces{e};
            for(const auto& c:code) {
                std::vector<Entity> next;
                for(const auto& part:pieces) {
                    int end=part.offset+part.length,code_end=c.offset+c.length;
                    if(end<=c.offset || part.offset>=code_end){next.push_back(part);continue;}
                    if(part.offset<c.offset){auto left=part;left.length=c.offset-part.offset;next.push_back(left);}
                    if(end>code_end){auto right=part;right.offset=code_end;right.length=end-code_end;next.push_back(right);}
                }
                pieces=std::move(next);
            }
            for(auto& part:pieces)decorated.push_back(std::move(part));
        }
        result_.entities=std::move(decorated);trim_entities(result_);
        auto key=[](const Entity& e){return std::tie(e.offset,e.length,e.type,e.document_id,e.url);};
        std::sort(result_.entities.begin(),result_.entities.end(),[&](const auto& a,const auto& b){return key(a)<key(b);});
        result_.entities.erase(std::unique(result_.entities.begin(),result_.entities.end(),[&](const auto& a,const auto& b){return key(a)==key(b);}),result_.entities.end());
    }
};

inline void premium(Text& result) {
    int offset=0;
    for(size_t i=0;i<result.text.size();) {
        auto n=character_bytes(result.text,i);int length=n==4?2:1;long long id=0;
        auto symbol=std::string_view(result.text).substr(i,n);
        if(symbol=="✅")id=success_emoji;
        else if(symbol=="❌")id=failure_emoji;
        else if(symbol=="🧹")id=clear_emoji;
        bool code=false;
        if(id) for(const auto& e:result.entities)
            if((e.type==KOTO_ENT_CODE || e.type==KOTO_ENT_PRE) && offset>=e.offset && offset<e.offset+e.length){code=true;break;}
        if(id && !code)result.entities.push_back({KOTO_ENT_CUSTOM_EMOJI,offset,length,id,{}});
        i+=n;offset+=length;
    }
}
inline Text plain(std::string text) {Text value{std::move(text),{}};premium(value);return value;}
inline Text markdown(std::string_view text) {auto value=Markdown{}.parse(text);premium(value);return value;}
inline Text slice(const Text& source,const std::string& text,int start) {
    Text result{text,{}};int end=start+units(text);
    for(const auto& e:source.entities) {
        int left=std::max(start,e.offset),right=std::min(end,e.offset+e.length);
        if(left<right)result.entities.push_back({e.type,left-start,right-left,e.document_id,e.url});
    }
    trim_entities(result);return result;
}
inline std::vector<Text> split(const Text& source,size_t limit=3500,unsigned depth=0) {
    std::vector<Text> result;int offset=0;
    for(const auto& text:split_text(source.text,limit)) {
        auto piece=slice(source,text,offset);offset+=units(text);
        // Telegram bounds both text and entity count. Split formatting rather
        // than dropping text or submitting an excessively dense entity list.
        if(piece.entities.size()>80 && units(text)>2 && depth<16) {
            auto more=split(piece,std::max<size_t>(2,static_cast<size_t>(units(text))/2),depth+1);
            for(auto& item:more)result.push_back(std::move(item));
        } else result.push_back(std::move(piece));
    }
    return result;
}
inline void compose(koto_ctx* ctx,const koto_api* api,const Text& source,int base=0) {
    api->c_text(ctx,source.text.c_str());
    if(api->c_entity) for(const auto& e:source.entities)
        api->c_entity(ctx,e.type,base+e.offset,e.length,e.document_id,e.url.c_str());
}
} // namespace gweb::ui

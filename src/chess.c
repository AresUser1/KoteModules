// Chess PvP for Kotogram, C ABI 3. Board coordinates: a8=0, h1=63.
// Build Android: aarch64-linux-android26-clang -std=c11 -O2 -shared -fPIC
//               -pthread -I/path/to/sdk/include chess.c -o chess.so
// No network or external chess engine; game state lives until module unload.
#include "module_abi.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { PAWN=1, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum { WK=1, WQ=2, BK=4, BQ=8 };
enum { EP=1, CASTLE=2 };
enum { EMPTY, LOBBY, PLAYING, FINISHED };
enum { MATE, STALEMATE, RESIGNED, AGREED, MATERIAL, REPETITION, FIFTY, FIVEFOLD, SEVENTYFIVE };
enum { MAX_GAMES=32, MAX_MOVES=256, HISTORY=160 };

typedef struct {
    int8_t board[64];             // Positive = white, negative = black.
    int turn, castle, ep, halfmove, fullmove;
} Position;
typedef struct { int from, to, promote, flags; } Move;
typedef struct { Move move[MAX_MOVES]; int count; } Moves;
typedef struct {
    char id[9];
    unsigned revision;
    int state, selected, promotion_to, winner, reason, last_from, last_to;
    Position pos;
    long long creator, white, black, chat, message, draw_by, rematch_by;
    char white_name[96], black_name[96], last_move[24];
    uint64_t history[HISTORY];
    int history_count;
    time_t touched;
} Game;

static Game games[MAX_GAMES];
static pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned id_counter;

static int side(int p) { return (p > 0) - (p < 0); }
static bool inside(int r, int c) { return r >= 0 && r < 8 && c >= 0 && c < 8; }

static void initial_position(Position* p) {
    static const int back[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
    memset(p, 0, sizeof(*p));
    for (int c=0; c<8; ++c) {
        p->board[c] = (int8_t)-back[c]; p->board[8+c] = -PAWN;
        p->board[48+c] = PAWN; p->board[56+c] = (int8_t)back[c];
    }
    p->turn=1; p->castle=WK|WQ|BK|BQ; p->ep=-1; p->fullmove=1;
}

// Attack detection deliberately includes pinned pieces (FIDE 3.9.1).
static bool attacked(const Position* p, int sq, int by) {
    int r=sq/8, c=sq%8, pawn_row=r+by;
    for (int d=-1; d<=1; d+=2)
        if (inside(pawn_row,c+d) && p->board[pawn_row*8+c+d]==by*PAWN) return true;
    static const int jump[8][2]={{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    for (int i=0; i<8; ++i) {
        int rr=r+jump[i][0], cc=c+jump[i][1];
        if (inside(rr,cc) && p->board[rr*8+cc]==by*KNIGHT) return true;
    }
    static const int dirs[8][2]={{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1}};
    for (int i=0; i<8; ++i) {
        for (int step=1;;++step) {
            int rr=r+step*dirs[i][0], cc=c+step*dirs[i][1];
            if (!inside(rr,cc)) break;
            int piece=p->board[rr*8+cc];
            if (!piece) continue;
            if (side(piece)==by) {
                int type=abs(piece);
                if (type==QUEEN || (i<4 && type==ROOK) || (i>=4 && type==BISHOP) ||
                    (step==1 && type==KING)) return true;
            }
            break;
        }
    }
    return false;
}

static bool in_check(const Position* p, int who) {
    for (int sq=0; sq<64; ++sq)
        if (p->board[sq]==who*KING) return attacked(p,sq,-who);
    return true; // A missing king must never make an illegal position playable.
}

static void revoke_rook(Position* p, int sq) {
    if (sq==0) p->castle &= ~BQ;
    if (sq==7) p->castle &= ~BK;
    if (sq==56) p->castle &= ~WQ;
    if (sq==63) p->castle &= ~WK;
}

// Only used with pseudo-legal/generated moves; public callbacks use legal_moves.
static void play(Position* p, Move m) {
    int piece=p->board[m.from], capture=p->board[m.to], who=p->turn;
    p->halfmove = (abs(piece)==PAWN || capture) ? 0 : p->halfmove+1;
    p->ep=-1;
    if (abs(piece)==KING) p->castle &= who==1 ? ~(WK|WQ) : ~(BK|BQ);
    revoke_rook(p,m.from); revoke_rook(p,m.to);
    p->board[m.from]=0;
    if (m.flags & EP) { p->board[m.to+who*8]=0; p->halfmove=0; }
    p->board[m.to]=(int8_t)(m.promote ? who*m.promote : piece);
    if (m.flags & CASTLE) {
        int rook_from=m.to>m.from ? m.from+3 : m.from-4;
        int rook_to=m.to>m.from ? m.from+1 : m.from-1;
        p->board[rook_to]=p->board[rook_from]; p->board[rook_from]=0;
    }
    if (abs(piece)==PAWN && abs(m.to-m.from)==16) p->ep=(m.from+m.to)/2;
    if (who==-1) ++p->fullmove;
    p->turn=-who;
}

static void add_legal(const Position* p, Moves* moves, Move m) {
    if (abs(p->board[m.to])==KING) return;
    Position next=*p;
    play(&next,m);
    if (!in_check(&next,p->turn) && moves->count<MAX_MOVES) moves->move[moves->count++]=m;
}

static void pawn_move(const Position* p, Moves* moves, int from, int to, int flags) {
    if (to/8==0 || to/8==7) {
        static const int types[4]={QUEEN,ROOK,BISHOP,KNIGHT};
        for (int i=0; i<4; ++i) add_legal(p,moves,(Move){from,to,types[i],flags});
    } else add_legal(p,moves,(Move){from,to,0,flags});
}

static void legal_moves(const Position* p, Moves* moves) {
    moves->count=0;
    static const int dirs[8][2]={{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1}};
    static const int jump[8][2]={{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    int who=p->turn;
    for (int from=0; from<64; ++from) {
        int type=abs(p->board[from]), r=from/8, c=from%8;
        if (side(p->board[from])!=who) continue;
        if (type==PAWN) {
            int rr=r-who;
            if (inside(rr,c) && !p->board[rr*8+c]) {
                pawn_move(p,moves,from,rr*8+c,0);
                if (r==(who==1 ? 6 : 1) && !p->board[(r-2*who)*8+c])
                    pawn_move(p,moves,from,(r-2*who)*8+c,0);
            }
            for (int dc=-1; dc<=1; dc+=2) {
                int cc=c+dc;
                if (!inside(rr,cc)) continue;
                int to=rr*8+cc;
                if (side(p->board[to])==-who) pawn_move(p,moves,from,to,0);
                else if (to==p->ep && !p->board[to] && p->board[to+who*8]==-who*PAWN)
                    pawn_move(p,moves,from,to,EP);
            }
        } else if (type==KNIGHT) {
            for (int i=0; i<8; ++i) {
                int rr=r+jump[i][0], cc=c+jump[i][1];
                if (inside(rr,cc) && side(p->board[rr*8+cc])!=who)
                    add_legal(p,moves,(Move){from,rr*8+cc,0,0});
            }
        } else {
            for (int i=0; i<8; ++i) {
                if ((type==BISHOP && i<4) || (type==ROOK && i>=4)) continue;
                for (int step=1; step<=7; ++step) {
                    int rr=r+step*dirs[i][0], cc=c+step*dirs[i][1];
                    if (!inside(rr,cc) || side(p->board[rr*8+cc])==who) break;
                    add_legal(p,moves,(Move){from,rr*8+cc,0,0});
                    if (p->board[rr*8+cc] || type==KING) break;
                }
            }
            int home=who==1 ? 60 : 4;
            if (type==KING && from==home && !in_check(p,who)) {
                for (int d=-1; d<=1; d+=2) {
                    int right=who==1 ? (d==1?WK:WQ) : (d==1?BK:BQ);
                    int rook=home+(d==1?3:-4);
                    if (!(p->castle & right) || p->board[rook]!=who*ROOK ||
                        p->board[home+d] || p->board[home+2*d] || (d==-1 && p->board[home-3])) continue;
                    // The transit square is tested with the king removed from e1/e8.
                    Position transit=*p;
                    transit.board[home]=0; transit.board[home+d]=(int8_t)(who*KING);
                    if (!attacked(&transit,home+d,-who))
                        add_legal(p,moves,(Move){home,home+2*d,0,CASTLE});
                }
            }
        }
    }
}

// En-passant only distinguishes repetitions if a legal capture actually exists.
static uint64_t position_key(const Position* p) {
    int ep=-1;
    if (p->ep>=0) {
        Moves moves; legal_moves(p,&moves);
        for (int i=0; i<moves.count; ++i) if (moves.move[i].flags & EP) ep=p->ep;
    }
    uint64_t h=UINT64_C(14695981039346656037);
    for (int i=0; i<64; ++i) { h^=(unsigned)(p->board[i]+KING); h*=UINT64_C(1099511628211); }
    h^=(unsigned)(p->turn+1); h*=UINT64_C(1099511628211);
    h^=(unsigned)p->castle; h*=UINT64_C(1099511628211);
    h^=(unsigned)(ep+1);
    return h;
}

static int repetitions(const Game* g) {
    uint64_t key=position_key(&g->pos);
    int count=0;
    for (int i=0; i<g->history_count; ++i) if (g->history[i]==key) ++count;
    return count;
}

// Conservative dead-material detection; e.g. K+NN vs K is NOT automatically dead.
static bool dead_material(const Position* p) {
    int minors=0, knights=0, bishop_color=-1;
    bool same_bishops=true;
    for (int sq=0; sq<64; ++sq) {
        int type=abs(p->board[sq]);
        if (!type || type==KING) continue;
        if (type!=KNIGHT && type!=BISHOP) return false;
        ++minors;
        if (type==KNIGHT) ++knights;
        else {
            int color=(sq/8+sq%8)%2;
            if (bishop_color>=0 && color!=bishop_color) same_bishops=false;
            bishop_color=color;
        }
    }
    return minors<=1 || (knights==0 && same_bishops);
}

static void finish(Game* g, int winner, int reason) {
    g->state=FINISHED; g->winner=winner; g->reason=reason;
    g->selected=-1; g->promotion_to=-1; g->draw_by=0; g->rematch_by=0;
}

static void adjudicate(Game* g) {
    Moves moves; legal_moves(&g->pos,&moves);
    if (!moves.count) finish(g,in_check(&g->pos,g->pos.turn)?-g->pos.turn:0,
                             in_check(&g->pos,g->pos.turn)?MATE:STALEMATE);
    else if (dead_material(&g->pos)) finish(g,0,MATERIAL);
    else if (repetitions(g)>=5) finish(g,0,FIVEFOLD);
    else if (g->pos.halfmove>=150) finish(g,0,SEVENTYFIVE);
}

static void reset_board(Game* g) {
    initial_position(&g->pos);
    g->selected=g->promotion_to=g->last_from=g->last_to=-1;
    g->winner=0; g->reason=0; g->draw_by=g->rematch_by=0;
    g->last_move[0]=0;
    g->history_count=1; g->history[0]=position_key(&g->pos);
}

static void commit_move(Game* g, Move m) {
    int rights=g->pos.castle, piece=g->pos.board[m.from], capture=g->pos.board[m.to];
    const char* sep=(capture || (m.flags&EP)) ? "×" : "–";
    static const char letters[]=" PNBRQK";
    if (m.flags & CASTLE) snprintf(g->last_move,sizeof(g->last_move),"%s",m.to>m.from?"O-O":"O-O-O");
    else snprintf(g->last_move,sizeof(g->last_move),"%c%c%s%c%c%s%c",
                  'a'+m.from%8,'8'-m.from/8,sep,'a'+m.to%8,'8'-m.to/8,
                  m.promote?"=":"",m.promote?letters[m.promote]:' ');
    play(&g->pos,m);
    if (!g->pos.halfmove || rights!=g->pos.castle) g->history_count=0;
    if (g->history_count==HISTORY) {
        memmove(g->history,g->history+1,(HISTORY-1)*sizeof(g->history[0])); --g->history_count;
    }
    g->history[g->history_count++]=position_key(&g->pos);
    g->last_from=m.from; g->last_to=m.to;
    g->selected=g->promotion_to=-1;
    // An offer by the side that just moved survives for the opponent's reply.
    long long mover=side(piece)==1?g->white:g->black;
    if (g->draw_by && g->draw_by!=mover) g->draw_by=0;
    adjudicate(g);
}

static const char* glyph(int piece) {
    static const char* white[]={"","♙","♘","♗","♖","♕","♔"};
    static const char* black[]={"","♟︎","♞","♝","♜","♛","♚"};
    return piece>0?white[piece]:black[-piece];
}

static void player_name(koto_ctx* ctx, const koto_api* api, long long uid, char out[96]) {
    out[0]=0;
    if (api->user_name(ctx,uid,out,96)<=0 || !out[0]) snprintf(out,96,"Игрок %lld",uid);
    out[95]=0;
    // Do not truncate inside UTF-8. Also remove line breaks from the header.
    size_t n=strlen(out);
    if (n==95) {
        size_t start=n-1;
        while (start && ((unsigned char)out[start]&0xc0)==0x80) --start;
        unsigned char lead=(unsigned char)out[start];
        size_t need=lead<0x80?1:lead<0xe0?2:lead<0xf0?3:4;
        if (start+need>n) out[start]=0;
    }
    for (char* s=out; *s; ++s) if (*s=='\n' || *s=='\r') *s=' ';
}

static void command_reply(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) api->c_edit(ctx,KOTO_FMT_PLAIN);
    else api->c_reply(ctx,KOTO_FMT_PLAIN);
}

static void message(koto_ctx* ctx, const koto_api* api, const char* text) {
    api->c_reset(ctx); api->c_text(ctx,text); command_reply(ctx,api);
}

static Game* find_game(const char* id) {
    for (int i=0; i<MAX_GAMES; ++i) if (games[i].state!=EMPTY && !strcmp(games[i].id,id)) return &games[i];
    return NULL;
}

static Game* allocate_game(void) {
    Game* oldest=NULL;
    for (int i=0; i<MAX_GAMES; ++i) {
        if (games[i].state==EMPTY) return &games[i];
        if (games[i].state==FINISHED && (!oldest || games[i].touched<oldest->touched)) oldest=&games[i];
    }
    return oldest; // Never silently evict an active game.
}

static void callback_data(const Game* g, const char* action, const char* arg, char out[64]) {
    snprintf(out,64,"chess:%s:%u:%s%s%s",g->id,g->revision,action,arg?":":"",arg?arg:"");
}

static void button(koto_ctx* ctx, const koto_api* api, const Game* g,
                   const char* text, const char* action, const char* arg, int row, int color, long long icon) {
    char data[64]; callback_data(g,action,arg,data);
    if (api->c_button_ex) api->c_button_ex(ctx,text,data,row,color,icon);
    else api->c_button(ctx,text,data,row);
}

static bool positive_id(const char* text, long long* id) {
    if (!text || !*text) return false;
    for (const char* s=text; *s; ++s) if (*s<'0' || *s>'9') return false;
    errno=0; char* end;
    long long value=strtoll(text,&end,10);
    if (errno || *end || value<=0) return false;
    *id=value; return true;
}

static const char* reason_text(int reason) {
    switch (reason) {
        case MATE: return "Мат";
        case STALEMATE: return "Пат";
        case RESIGNED: return "Соперник сдался";
        case AGREED: return "Ничья по соглашению";
        case MATERIAL: return "Недостаточно материала для мата";
        case REPETITION: return "Троекратное повторение позиции";
        case FIFTY: return "Правило 50 ходов";
        case FIVEFOLD: return "Пятикратное повторение позиции";
        default: return "Правило 75 ходов";
    }
}

static void render(koto_ctx* ctx, const koto_api* api, Game* g) {
    char text[512];
    api->c_reset(ctx);
    api->c_fmt(ctx,"♔ Шахматы\n",KOTO_ENT_BOLD);
    snprintf(text,sizeof(text),"⚪ %s\n⚫ %s\nПартия %s\n",
             g->white?g->white_name:"Свободно",g->black?g->black_name:"Свободно",g->id);
    api->c_text(ctx,text);
    if (g->state==LOBBY) {
        api->c_text(ctx,"\nВыберите цвет. Играть могут любые два участника чата.");
        if (!g->white) button(ctx,api,g,"⚪ Играть за белых","join","w",1,0,0);
        if (!g->black) button(ctx,api,g,"⚫ Играть за чёрных","join","b",1,0,0);
        button(ctx,api,g,"✖ Отменить вызов","close",NULL,1,0,0);
        return;
    }
    if (g->state==FINISHED) {
        if (g->winner) snprintf(text,sizeof(text),"\n🏆 Победили %s: %s\n%s\n",
                               g->winner==1?"белые":"чёрные",g->winner==1?g->white_name:g->black_name,reason_text(g->reason));
        else snprintf(text,sizeof(text),"\n🤝 %s\n",reason_text(g->reason));
        api->c_fmt(ctx,text,KOTO_ENT_BOLD);
    } else {
        snprintf(text,sizeof(text),"\nХод %d: %s %s%s\n",g->pos.fullmove,g->pos.turn==1?"⚪":"⚫",
                 g->pos.turn==1?g->white_name:g->black_name,in_check(&g->pos,g->pos.turn)?" — ШАХ!":"");
        api->c_fmt(ctx,text,KOTO_ENT_BOLD);
    }
    if (g->last_move[0]) { api->c_text(ctx,"Последний ход: "); api->c_text(ctx,g->last_move); api->c_text(ctx,"\n"); }
    api->c_text(ctx,"Доска: 8 → 1 сверху вниз, a → h слева направо.\n");
    if (g->state==PLAYING) api->c_text(ctx,g->promotion_to>=0?"Выберите фигуру превращения ниже.":"Нажмите фигуру, затем подсвеченную клетку.");

    Moves moves; legal_moves(&g->pos,&moves);
    bool check=in_check(&g->pos,g->pos.turn);
    for (int sq=0; sq<64; ++sq) {
        int piece=g->pos.board[sq], color=KOTO_BTN_DEFAULT;
        bool target=false;
        if (g->state==PLAYING && g->selected>=0 && g->promotion_to<0)
            for (int i=0; i<moves.count; ++i)
                if (moves.move[i].from==g->selected && moves.move[i].to==sq) target=true;
        if (target) color=piece?KOTO_BTN_DANGER:KOTO_BTN_SUCCESS;
        if (g->selected==sq) color=KOTO_BTN_PRIMARY;
        if (check && piece==g->pos.turn*KING) color=KOTO_BTN_DANGER;
        const char* label=piece?glyph(piece):((sq/8+sq%8)%2?"▪":"▫");
        if (target && !piece) label="•";
        char arg[16]; snprintf(arg,sizeof(arg),"%d",sq);
        button(ctx,api,g,label,g->state==PLAYING?"sq":"noop",g->state==PLAYING?arg:NULL,
               sq%8==0,color,0);
    }
    for (int c=0; c<8; ++c) {
        char file[2]={(char)('a'+c),0}; button(ctx,api,g,file,"noop",NULL,c==0,0,0);
    }
    if (g->state==FINISHED) {
        if (g->rematch_by) {
            api->c_text(ctx,"\nРеванш предложен — решение за соперником.");
            button(ctx,api,g,"✅ Принять реванш","accept",NULL,1,0,0);
            button(ctx,api,g,"✖ Отказаться","reject",NULL,0,0,0);
        } else button(ctx,api,g,"🔄 Реванш со сменой сторон","rematch",NULL,1,0,0);
        button(ctx,api,g,"✖ Закрыть","close",NULL,1,0,0);
        return;
    }
    if (g->promotion_to>=0) {
        static const char* labels[]={"Ферзь","Ладья","Слон","Конь"};
        static const char* codes[]={"q","r","b","n"};
        static const int types[]={QUEEN,ROOK,BISHOP,KNIGHT};
        for (int i=0; i<4; ++i) {
            snprintf(text,sizeof(text),"%s %s",glyph(types[i]*g->pos.turn),labels[i]);
            button(ctx,api,g,text,"promote",codes[i],i%2==0,0,0);
        }
        button(ctx,api,g,"↩ Выбрать другой ход","back",NULL,1,0,0);
    }
    if (g->draw_by) {
        snprintf(text,sizeof(text),"\n🤝 %s предлагает ничью.",g->draw_by==g->white?g->white_name:g->black_name);
        api->c_text(ctx,text);
        button(ctx,api,g,"✅ Принять ничью","agree",NULL,1,0,0);
        button(ctx,api,g,"✖ Отклонить / отозвать","decline",NULL,0,0,0);
    } else button(ctx,api,g,"🤝 Предложить ничью","draw",NULL,1,0,0);
    if (g->pos.halfmove>=100 || repetitions(g)>=3)
        button(ctx,api,g,"⚖ Заявить ничью","claim",NULL,1,0,0);
    button(ctx,api,g,"🏳 Сдаться","resign",NULL,1,0,0);
}

static void cmd_chess(koto_ctx* ctx, const koto_api* api) {
    long long uid=api->sender_id(ctx);
    if (uid<=0) { message(ctx,api,"Начните игру от имени пользователя."); return; }
    char who[96]; player_name(ctx,api,uid,who);
    const char* args=api->cmd_args(ctx);
    while (*args && isspace((unsigned char)*args)) ++args;
    char arg[32]; snprintf(arg,sizeof(arg),"%s",args);
    size_t n=strlen(arg); while (n && isspace((unsigned char)arg[n-1])) arg[--n]=0;
    int color=0;
    if (!strcmp(arg,"white") || !strcmp(arg,"w") || !strcmp(arg,"белые")) color=1;
    else if (!strcmp(arg,"black") || !strcmp(arg,"b") || !strcmp(arg,"черные") || !strcmp(arg,"чёрные")) color=-1;
    else if (*arg) { message(ctx,api,"Используйте .chess [white|black] или .шахматы. Справка: .chesshelp"); return; }
    pthread_mutex_lock(&game_mutex);
    Game* g=allocate_game();
    if (!g) { pthread_mutex_unlock(&game_mutex); message(ctx,api,"Достигнут лимит 32 партий. Закройте свою через .chessend <id>."); return; }
    memset(g,0,sizeof(*g));
    do { snprintf(g->id,sizeof(g->id),"%08x",(unsigned)time(NULL)^(++id_counter*2654435761u)); } while (find_game(g->id));
    g->state=LOBBY; g->revision=1; g->creator=uid; g->chat=api->chat_id(ctx); g->touched=time(NULL);
    reset_board(g);
    if (color==1) { g->white=uid; memcpy(g->white_name,who,sizeof(who)); }
    if (color==-1) { g->black=uid; memcpy(g->black_name,who,sizeof(who)); }
    render(ctx,api,g); command_reply(ctx,api);
    pthread_mutex_unlock(&game_mutex);
}

static bool participant(const Game* g, long long uid) { return uid>0 && (uid==g->white || uid==g->black); }

static void cmd_chessend(koto_ctx* ctx, const koto_api* api) {
    const char* args=api->cmd_args(ctx);
    while (*args && isspace((unsigned char)*args)) ++args;
    long long uid=api->sender_id(ctx), chat=api->chat_id(ctx), owner=api->get_me(ctx);
    pthread_mutex_lock(&game_mutex);
    if (*args) {
        char id[16]; snprintf(id,sizeof(id),"%s",args);
        size_t n=strlen(id); while (n && isspace((unsigned char)id[n-1])) id[--n]=0;
        Game* g=find_game(id);
        if (!g || g->chat!=chat) message(ctx,api,"Партия в этом чате не найдена.");
        else if (uid!=owner && uid!=g->creator && !participant(g,uid)) message(ctx,api,"Закрыть партию могут её создатель, игроки или владелец юзербота.");
        else { g->state=EMPTY; message(ctx,api,"♔ Партия закрыта. Её кнопки больше не действуют."); }
    } else {
        api->c_reset(ctx); api->c_text(ctx,"♔ Партии в этом чате:\n");
        int count=0;
        for (int i=0; i<MAX_GAMES; ++i) if (games[i].state!=EMPTY && games[i].chat==chat) {
            char line[300];
            snprintf(line,sizeof(line),"%.8s — ⚪ %lld / ⚫ %lld (%s)\n",games[i].id,
                     games[i].white,games[i].black,
                     games[i].state==LOBBY?"ожидание":games[i].state==PLAYING?"игра":"завершена");
            api->c_text(ctx,line); ++count;
        }
        api->c_text(ctx,count?"Закрыть: .chessend <id>":"Нет партий."); command_reply(ctx,api);
    }
    pthread_mutex_unlock(&game_mutex);
}

static void cmd_chesshelp(koto_ctx* ctx, const koto_api* api) {
    message(ctx,api,
        "♔ Шахматы для двоих\n\n.chess [white|black] / .шахматы — создать вызов.\n"
        "Соперник выбирает свободную сторону. Нажмите фигуру и подсвеченную клетку.\n"
        "Рокировка: e1→g1/c1 или e8→g8/c8. Пешка превращается в ферзя, ладью, слона или коня.\n"
        "Есть шах, мат, пат, взятие на проходе, сдача, ничья и реванш.\n"
        "Троекратное повторение / 50 ходов: заявление за текущую позицию. Пятикратное / 75 ходов — автоматически.\n"
        ".chessend [id] — список партий / закрыть свою. Часов и компьютера-соперника нет.\n"
        "Фигуры: ♔♕♖♗♘♙ / ♚♛♜♝♞♟︎. Premium не нужен.\n"
        "Партии хранятся до перезапуска или выгрузки модуля.");
}

typedef struct { char id[9], action[12], arg[12]; unsigned revision; } Callback;

static bool parse_callback(const char* data, Callback* cb) {
    if (!data || strncmp(data,"chess:",6) || strlen(data)>=64 || strstr(data,"::") || data[strlen(data)-1]==':') return false;
    char buffer[64]; snprintf(buffer,sizeof(buffer),"%s",data+6);
    char* parts[4]; int n=0; char* token=buffer;
    while (token) {
        if (n==4) return false;
        parts[n++]=token;
        char* colon=strchr(token,':');
        if (colon) { *colon=0; token=colon+1; } else token=NULL;
    }
    if (n<3 || strlen(parts[0])!=8 || strlen(parts[2])>=sizeof(cb->action) || (n==4 && strlen(parts[3])>=sizeof(cb->arg))) return false;
    for (int i=0; i<8; ++i) if (!isxdigit((unsigned char)parts[0][i])) return false;
    if (!*parts[1]) return false;
    for (const char* s=parts[1]; *s; ++s) if (!isdigit((unsigned char)*s)) return false;
    errno=0; char* end; unsigned long rev=strtoul(parts[1],&end,10);
    if (errno || *end || rev>UINT_MAX) return false;
    snprintf(cb->id,sizeof(cb->id),"%s",parts[0]); cb->revision=(unsigned)rev;
    snprintf(cb->action,sizeof(cb->action),"%s",parts[2]);
    snprintf(cb->arg,sizeof(cb->arg),"%s",n==4?parts[3]:"");
    return true;
}

static bool click_square(Game* g, int sq) {
    if (g->promotion_to>=0) return false;
    if (g->selected==sq) { g->selected=-1; return true; }
    Moves moves; legal_moves(&g->pos,&moves);
    for (int i=0; i<moves.count; ++i) {
        Move m=moves.move[i];
        if (m.from==g->selected && m.to==sq) {
            if (m.promote) g->promotion_to=sq;
            else commit_move(g,m);
            return true;
        }
    }
    if (side(g->pos.board[sq])==g->pos.turn) {
        for (int i=0; i<moves.count; ++i) if (moves.move[i].from==sq) { g->selected=sq; return true; }
    }
    return false;
}

static void handle_callback(koto_ctx* ctx, const koto_api* api, const Callback* cb) {
    long long uid=api->sender_id(ctx), chat=api->chat_id(ctx), mid=api->cb_message_id(ctx);
    Game* g=find_game(cb->id);
    if (!g || (chat && chat!=g->chat) || (g->message && mid && g->message!=mid)) {
        api->cb_answer(ctx,"Эта партия закрыта или кнопка принадлежит другой доске.",1); return;
    }
    if (cb->revision!=g->revision) { api->cb_answer(ctx,"Доска уже изменилась. Нажмите кнопку на обновлённой доске.",0); return; }
    const char* action=cb->action;
    if (!strcmp(action,"noop")) { api->cb_answer(ctx,"",0); return; }
    if (uid<=0) { api->cb_answer(ctx,"Нужен аккаунт пользователя.",1); return; }
    const char* answer="";
    bool member=participant(g,uid);
    if (!strcmp(action,"join") && g->state==LOBBY && (!strcmp(cb->arg,"w") || !strcmp(cb->arg,"b"))) {
        bool white=cb->arg[0]=='w';
        if (member) { api->cb_answer(ctx,"Вы уже заняли сторону. Дождитесь другого игрока.",1); return; }
        if ((white && g->white) || (!white && g->black)) { api->cb_answer(ctx,"Эта сторона уже занята.",1); return; }
        char who[96]; player_name(ctx,api,uid,who);
        if (white) { g->white=uid; memcpy(g->white_name,who,sizeof(who)); }
        else { g->black=uid; memcpy(g->black_name,who,sizeof(who)); }
        if (g->white && g->black) { g->state=PLAYING; answer="Партия началась. Ход белых!"; }
        else answer="Сторона выбрана. Ждём соперника.";
    } else if (!strcmp(action,"close") && (g->state==LOBBY || g->state==FINISHED)) {
        if (!member && uid!=g->creator && uid!=api->get_me(ctx)) { api->cb_answer(ctx,"Закрыть доску могут её игроки или создатель.",1); return; }
        g->state=EMPTY;
        api->cb_answer(ctx,"Партия закрыта.",0); api->c_reset(ctx);
        api->c_text(ctx,"♔ Партия закрыта."); api->c_edit_message(ctx,chat,mid,KOTO_FMT_PLAIN); return;
    } else if (!member) { api->cb_answer(ctx,"Вы не участник этой партии.",1); return;
    } else if (g->state==PLAYING) {
        long long current=g->pos.turn==1?g->white:g->black;
        bool move_action=!strcmp(action,"sq") || !strcmp(action,"promote") || !strcmp(action,"back") || !strcmp(action,"claim");
        if (move_action && uid!=current) { api->cb_answer(ctx,"Сейчас ход соперника.",0); return; }
        if (!strcmp(action,"sq")) {
            long long number;
            bool zero=!strcmp(cb->arg,"0");
            if ((!zero && !positive_id(cb->arg,&number)) || (!zero && number>=64)) { api->cb_answer(ctx,"Некорректная клетка.",1); return; }
            int sq=zero?0:(int)number;
            if (!click_square(g,sq)) { api->cb_answer(ctx,g->promotion_to>=0?"Сначала выберите фигуру превращения.":"Недопустимый ход. Выберите свою подвижную фигуру.",0); return; }
        } else if (!strcmp(action,"promote") && g->promotion_to>=0) {
            int promote=!strcmp(cb->arg,"q")?QUEEN:!strcmp(cb->arg,"r")?ROOK:!strcmp(cb->arg,"b")?BISHOP:!strcmp(cb->arg,"n")?KNIGHT:0;
            Moves moves; legal_moves(&g->pos,&moves); bool found=false;
            for (int i=0; i<moves.count; ++i) {
                Move m=moves.move[i];
                if (m.from==g->selected && m.to==g->promotion_to && m.promote==promote) { commit_move(g,m); found=true; break; }
            }
            if (!found) { api->cb_answer(ctx,"Выберите ферзя, ладью, слона или коня.",0); return; }
        } else if (!strcmp(action,"back") && g->promotion_to>=0) { g->promotion_to=-1; g->selected=-1;
        } else if (!strcmp(action,"resign")) { finish(g,uid==g->white?-1:1,RESIGNED); answer="Вы сдались.";
        } else if (!strcmp(action,"draw")) {
            if (g->draw_by) { api->cb_answer(ctx,"Предложение ничьей уже ожидает ответа.",0); return; }
            g->draw_by=uid; answer="Сопернику предложена ничья.";
        } else if (!strcmp(action,"agree") && g->draw_by && g->draw_by!=uid) { finish(g,0,AGREED); answer="Ничья согласована.";
        } else if (!strcmp(action,"decline") && g->draw_by) { g->draw_by=0; answer="Предложение ничьей снято.";
        } else if (!strcmp(action,"claim") && (g->pos.halfmove>=100 || repetitions(g)>=3)) {
            finish(g,0,repetitions(g)>=3?REPETITION:FIFTY); answer="Ничья зафиксирована.";
        } else { api->cb_answer(ctx,"Это действие сейчас недоступно.",0); return; }
    } else if (g->state==FINISHED) {
        if (!strcmp(action,"rematch") && !g->rematch_by) { g->rematch_by=uid; answer="Сопернику предложен реванш.";
        } else if (!strcmp(action,"accept") && g->rematch_by && g->rematch_by!=uid) {
            long long swap=g->white; g->white=g->black; g->black=swap;
            char name[96]; memcpy(name,g->white_name,sizeof(name)); memcpy(g->white_name,g->black_name,sizeof(name)); memcpy(g->black_name,name,sizeof(name));
            reset_board(g); g->state=PLAYING; answer="Реванш! Стороны поменялись.";
        } else if (!strcmp(action,"reject") && g->rematch_by) { g->rematch_by=0; answer="Предложение реванша снято.";
        } else { api->cb_answer(ctx,"Для реванша нужно согласие соперника.",0); return; }
    } else { api->cb_answer(ctx,"Дождитесь второго игрока.",0); return; }
    if (mid && !g->message) g->message=mid;
    ++g->revision; g->touched=time(NULL);
    api->cb_answer(ctx,answer,0); render(ctx,api,g); api->c_edit_message(ctx,chat,mid,KOTO_FMT_PLAIN);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* data=api->cb_data(ctx);
    if (!data || strncmp(data,"chess:",6)) return;
    Callback cb;
    if (!parse_callback(data,&cb)) { api->cb_answer(ctx,"Некорректная кнопка шахмат.",0); return; }
    pthread_mutex_lock(&game_mutex); handle_callback(ctx,api,&cb); pthread_mutex_unlock(&game_mutex);
}

static const koto_command COMMANDS[]={
    {"chess",cmd_chess,KOTO_LEVEL_ALL,"Шахматы PvP: создать вызов","[white|black]"},
    {"шахматы",cmd_chess,KOTO_LEVEL_ALL,"Шахматы PvP: создать вызов","[white|black]"},
    {"chessend",cmd_chessend,KOTO_LEVEL_ALL,"Партии в чате / закрыть свою","[id]"},
    {"chesshelp",cmd_chesshelp,KOTO_LEVEL_ALL,"Правила управления шахматами",NULL},
};
static const koto_module MODULE={
    .abi_version=KOTO_MODULE_ABI, .name="chess",
    .description="Шахматы PvP на кнопках: рокировка, en passant, превращение, мат, ничьи и реванш",
    .version="1.0.0", .min_host_version=56, .flags=0,
    .commands=COMMANDS, .command_count=(int)(sizeof(COMMANDS)/sizeof(COMMANDS[0])),
    .on_callback=on_callback
};
const koto_module* koto_module_register(void) { return &MODULE; }

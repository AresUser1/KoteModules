// Offline rules and callback tests. No Telegram account or network involved.
// Compile with -std=c11 -O2 -pthread -Wall -Wextra -Werror -pedantic,
// and -I../kote_module_sdk/include tests/test_chess.c -o /tmp/test_chess.
#include <assert.h>
#include "../src/chess.c"

static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); exit(1); } } while (0)

static int sq(const char* s) { return ('8'-s[1])*8+s[0]-'a'; }

static Position fen(const char* text) {
    Position p={.turn=1,.ep=-1,.fullmove=1};
    int index=0;
    const char* chars="pnbrqk";
    while (*text && *text!=' ') {
        unsigned char c=(unsigned char)*text++;
        if (c=='/') continue;
        if (c>='1' && c<='8') index+=c-'0';
        else {
            const char* piece=strchr(chars,tolower(c)); assert(piece && index<64);
            p.board[index++]=(int8_t)((piece-chars+1)*(isupper(c)?1:-1));
        }
    }
    assert(index==64 && *text==' '); ++text;
    p.turn=*text=='w'?1:-1;
    text=strchr(text,' ')+1;
    while (*text && *text!=' ') {
        if (*text=='K') p.castle|=WK;
        if (*text=='Q') p.castle|=WQ;
        if (*text=='k') p.castle|=BK;
        if (*text=='q') p.castle|=BQ;
        ++text;
    }
    if (*text) ++text;
    if (*text && *text!='-') p.ep=sq(text);
    const char* clocks=strchr(text,' ');
    if (clocks) { p.halfmove=atoi(clocks+1); clocks=strchr(clocks+1,' '); if (clocks) p.fullmove=atoi(clocks+1); }
    return p;
}

static Move get_move(const Position* p, const char* uci) {
    int promote=0;
    if (uci[4]) {
        const char* types="pnbrqk"; const char* ch=strchr(types,uci[4]);
        assert(ch); promote=(int)(ch-types)+1;
    }
    Moves moves; legal_moves(p,&moves);
    for (int i=0; i<moves.count; ++i)
        if (moves.move[i].from==sq(uci) && moves.move[i].to==sq(uci+2) && moves.move[i].promote==promote) return moves.move[i];
    fprintf(stderr,"Expected legal move: %s\n",uci); exit(1);
}

static bool has_move(const Position* p, const char* uci) {
    Moves moves; legal_moves(p,&moves);
    for (int i=0; i<moves.count; ++i)
        if (moves.move[i].from==sq(uci) && moves.move[i].to==sq(uci+2)) return true;
    return false;
}

static uint64_t perft(const Position* p, int depth) {
    if (!depth) return 1;
    Moves moves; legal_moves(p,&moves);
    if (depth==1) return (uint64_t)moves.count;
    uint64_t count=0;
    for (int i=0; i<moves.count; ++i) { Position next=*p; play(&next,moves.move[i]); count+=perft(&next,depth-1); }
    return count;
}

static void rules_tests(void) {
    Position p; initial_position(&p);
    CHECK(perft(&p,1)==20); CHECK(perft(&p,2)==400); CHECK(perft(&p,3)==8902); CHECK(perft(&p,4)==197281);
    // Standard independent perft fixtures, also used by Stockfish:
    // https://github.com/official-stockfish/Stockfish/blob/master/tests/perft.sh
    p=fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    CHECK(perft(&p,1)==48); CHECK(perft(&p,2)==2039); CHECK(perft(&p,3)==97862);
    p=fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    CHECK(perft(&p,1)==14); CHECK(perft(&p,2)==191); CHECK(perft(&p,3)==2812); CHECK(perft(&p,4)==43238);
    p=fen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
    CHECK(perft(&p,1)==6); CHECK(perft(&p,2)==264); CHECK(perft(&p,3)==9467);
    p=fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
    CHECK(perft(&p,1)==44); CHECK(perft(&p,2)==1486); CHECK(perft(&p,3)==62379);
    p=fen("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
    CHECK(perft(&p,1)==46); CHECK(perft(&p,2)==2079); CHECK(perft(&p,3)==89890);

    p=fen("4kr2/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(!has_move(&p,"e1g1")); CHECK(has_move(&p,"e1c1"));
    play(&p,get_move(&p,"e1c1"));
    CHECK(p.board[sq("c1")]==KING && p.board[sq("d1")]==ROOK && p.board[sq("a1")]==0);
    CHECK(!(p.castle&(WK|WQ)));
    p=fen("4k3/8/8/8/8/8/8/R3K3 w KQ - 0 1");
    CHECK(!has_move(&p,"e1g1")); // Rights alone cannot conjure a missing rook.
    p=fen("k3r3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(in_check(&p,1) && !has_move(&p,"e1g1") && !has_move(&p,"e1c1"));
    p=fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    play(&p,get_move(&p,"a1a8"));
    CHECK(!(p.castle&(WQ|BQ)) && (p.castle&(WK|BK))==(WK|BK));

    p=fen("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    CHECK(!has_move(&p,"e5d6")); // En passant would expose the king.
    Position no_ep=p; no_ep.ep=-1;
    CHECK(position_key(&p)==position_key(&no_ep));
    p=fen("8/8/8/r4pPK/8/8/8/k7 w - f6 0 1");
    CHECK(!has_move(&p,"g5f6")); // En passant removes TWO blockers from a rank.
    p=fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    no_ep=p; no_ep.ep=-1;
    CHECK(position_key(&p)!=position_key(&no_ep));
    play(&p,get_move(&p,"e5d6"));
    CHECK(p.board[sq("d6")]==PAWN && !p.board[sq("d5")] && p.ep==-1 && p.halfmove==0);
    p=fen("4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1");
    play(&p,get_move(&p,"e4d3")); CHECK(p.board[sq("d3")]==-PAWN && !p.board[sq("d4")]);
    p=fen("4k3/4n3/8/8/6K1/8/8/4R3 w - - 0 1");
    CHECK(!has_move(&p,"g4f5")); // The pinned knight still attacks f5.
    p=fen("7k/P7/8/8/8/8/8/7K w - - 0 1");
    Moves moves; legal_moves(&p,&moves); int promotions=0;
    for (int i=0; i<moves.count; ++i) if (moves.move[i].from==sq("a7")) ++promotions;
    CHECK(promotions==4);
    play(&p,get_move(&p,"a7a8n")); CHECK(p.board[0]==KNIGHT);
    p=fen("7k/8/8/8/8/8/p7/7K b - - 0 1");
    play(&p,get_move(&p,"a2a1r")); CHECK(p.board[56]==-ROOK);

    Game g={.state=PLAYING,.white=1,.black=2}; reset_board(&g);
    const char* mate[]={"f2f3","e7e5","g2g4","d8h4"};
    for (int i=0; i<4; ++i) commit_move(&g,get_move(&g.pos,mate[i]));
    CHECK(g.state==FINISHED && g.winner==-1 && g.reason==MATE);
    g.state=PLAYING; g.pos=fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"); adjudicate(&g);
    CHECK(g.state==FINISHED && !g.winner && g.reason==STALEMATE);
    p=fen("7k/8/8/8/8/8/8/K7 w - - 0 1"); CHECK(dead_material(&p));
    p=fen("7k/8/8/8/8/8/8/KN6 w - - 0 1"); CHECK(dead_material(&p));
    p=fen("7k/8/8/8/8/8/8/KNN5 w - - 0 1"); CHECK(!dead_material(&p));
    p=fen("7k/8/8/8/8/8/8/KBB5 w - - 0 1"); CHECK(!dead_material(&p));
    p=fen("7k/8/8/8/8/4b3/8/K1B5 w - - 0 1"); CHECK(dead_material(&p));

    g.state=PLAYING; reset_board(&g);
    const char* cycle[]={"g1f3","g8f6","f3g1","f6g8"};
    for (int round=0; round<2; ++round) for (int i=0; i<4; ++i) commit_move(&g,get_move(&g.pos,cycle[i]));
    CHECK(repetitions(&g)==3 && g.state==PLAYING);
    for (int round=0; round<2; ++round) for (int i=0; i<4; ++i) commit_move(&g,get_move(&g.pos,cycle[i]));
    CHECK(repetitions(&g)==5 && g.state==FINISHED && g.reason==FIVEFOLD);
    g.state=PLAYING; g.pos=fen("7k/8/8/8/8/8/8/R5K1 w - - 149 1");
    commit_move(&g,get_move(&g.pos,"a1a2")); CHECK(g.state==FINISHED && g.reason==SEVENTYFIVE);
    g.state=PLAYING; g.pos=fen("7k/8/5KQ1/8/8/8/8/8 w - - 149 1");
    commit_move(&g,get_move(&g.pos,"g6g7")); CHECK(g.state==FINISHED && g.winner==1 && g.reason==MATE);
    initial_position(&p); no_ep=p; no_ep.castle &= ~WK;
    CHECK(position_key(&p)!=position_key(&no_ep));
    printf("Rules and perft: %d checks passed\n",checks);
}

struct koto_ctx {
    long long uid, chat, mid, entity;
    const char* args;
    const char* callback;
    char text[16000], answer[512];
    struct { char text[64], data[64]; int row, color; long long icon; } buttons[128];
    int count, edits, replies, outgoing;
};
static long long mock_uid(koto_ctx* c) { return c->uid; }
static long long mock_chat(koto_ctx* c) { return c->chat; }
static long long mock_mid(koto_ctx* c) { return c->mid; }
static long long mock_me(koto_ctx* c) { (void)c; return 1; }
static int mock_out(koto_ctx* c) { return c->outgoing; }
static const char* mock_args(koto_ctx* c) { return c->args; }
static const char* mock_cb(koto_ctx* c) { return c->callback; }
static int mock_name(koto_ctx* c, long long uid, char* out, int n) { (void)c; return snprintf(out,(size_t)n,"Player %lld",uid); }
static void mock_reset(koto_ctx* c) { c->text[0]=0; c->count=0; }
static void mock_text(koto_ctx* c, const char* text) { assert(strlen(c->text)+strlen(text)<sizeof(c->text)); strcat(c->text,text); }
static void mock_fmt(koto_ctx* c, const char* text, int type) { (void)type; mock_text(c,text); }
static void mock_button_ex(koto_ctx* c, const char* text, const char* data, int row, int color, long long icon) {
    assert(c->count<128 && *text && strlen(text)<64 && strlen(data)<64);
    int i=c->count++;
    snprintf(c->buttons[i].text,64,"%s",text); snprintf(c->buttons[i].data,64,"%s",data);
    c->buttons[i].row=row; c->buttons[i].color=color; c->buttons[i].icon=icon;
}
static void mock_button(koto_ctx* c, const char* text, const char* data, int row) { mock_button_ex(c,text,data,row,0,0); }
static void mock_edit(koto_ctx* c, int flags) { (void)flags; ++c->edits; }
static void mock_reply(koto_ctx* c, int flags) { (void)flags; ++c->replies; }
static void mock_edit_msg(koto_ctx* c, long long chat, long long mid, int flags) { assert(chat==c->chat && mid==c->mid); mock_edit(c,flags); }
static void mock_answer(koto_ctx* c, const char* text, int alert) { (void)alert; snprintf(c->answer,sizeof(c->answer),"%s",text); }
static const koto_api api={
    .sender_id=mock_uid,.chat_id=mock_chat,.cb_message_id=mock_mid,.get_me=mock_me,.is_outgoing=mock_out,
    .cmd_args=mock_args,.cb_data=mock_cb,.user_name=mock_name,.c_reset=mock_reset,.c_text=mock_text,
    .c_fmt=mock_fmt,.c_button=mock_button,.c_button_ex=mock_button_ex,.c_edit=mock_edit,.c_reply=mock_reply,
    .c_edit_message=mock_edit_msg,.cb_answer=mock_answer,
};

static void click(koto_ctx* c, Game* g, long long uid, const char* action, const char* arg) {
    char data[64]; callback_data(g,action,arg,data);
    c->uid=uid; c->callback=data; c->answer[0]=0; on_callback(c,&api); c->callback=NULL;
}

static Game* new_game(koto_ctx* c) {
    memset(games,0,sizeof(games));
    *c=(koto_ctx){.uid=1,.chat=100,.mid=42,.args="white",.outgoing=1};
    cmd_chess(c,&api); CHECK(games[0].state==LOBBY && games[0].white==1);
    return &games[0];
}

static void ui_tests(void) {
    koto_ctx c; Game* g=new_game(&c);
    click(&c,g,1,"join","b"); CHECK(g->state==LOBBY && !g->black);
    click(&c,g,2,"join","b"); CHECK(g->state==PLAYING && g->black==2);
    CHECK(c.count>=74 && c.count<100 && c.edits>=2);
    int row_count=0;
    for (int i=0; i<c.count; ++i) { if (c.buttons[i].row) row_count=0; CHECK(++row_count<=8); }
    unsigned rev=g->revision;
    click(&c,g,3,"resign",NULL); CHECK(g->state==PLAYING && g->revision==rev);
    click(&c,g,2,"sq","12"); CHECK(g->selected==-1 && g->revision==rev);
    char stale[64]; callback_data(g,"sq","52",stale);
    click(&c,g,1,"sq","52"); CHECK(g->selected==52);
    bool hint=false;
    for (int i=0; i<c.count; ++i) if (c.buttons[i].color==KOTO_BTN_SUCCESS) hint=true;
    CHECK(hint);
    rev=g->revision; c.callback=stale; on_callback(&c,&api); CHECK(g->revision==rev && g->selected==52);
    click(&c,g,1,"sq","36"); CHECK(g->pos.board[36]==PAWN && !g->pos.board[52] && g->pos.turn==-1);
    rev=g->revision; c.mid=99; click(&c,g,2,"sq","12"); CHECK(g->revision==rev); c.mid=42;
    c.chat=200; click(&c,g,2,"sq","12"); CHECK(g->revision==rev); c.chat=100;
    click(&c,g,1,"draw",NULL); CHECK(g->draw_by==1);
    click(&c,g,1,"agree",NULL); CHECK(g->state==PLAYING);
    click(&c,g,2,"agree",NULL); CHECK(g->state==FINISHED && g->reason==AGREED);
    click(&c,g,1,"accept",NULL); CHECK(g->state==FINISHED);
    click(&c,g,1,"rematch",NULL); CHECK(g->rematch_by==1);
    click(&c,g,1,"accept",NULL); CHECK(g->state==FINISHED);
    click(&c,g,2,"accept",NULL); CHECK(g->state==PLAYING && g->white==2 && g->black==1 && g->pos.turn==1);
    click(&c,g,2,"resign",NULL); CHECK(g->state==FINISHED && g->winner==-1);
    click(&c,g,3,"close",NULL); CHECK(g->state==FINISHED);
    click(&c,g,2,"close",NULL); CHECK(g->state==EMPTY);

    g=new_game(&c); click(&c,g,2,"join","b");
    g->pos=fen("7k/P7/8/8/8/8/8/7K w - - 0 1");
    click(&c,g,1,"sq","8"); click(&c,g,1,"sq","0");
    CHECK(g->promotion_to==0 && g->pos.board[8]==PAWN && !g->pos.board[0] && g->pos.turn==1);
    click(&c,g,2,"promote","q"); CHECK(g->promotion_to==0 && g->pos.board[8]==PAWN);
    click(&c,g,1,"promote","k"); CHECK(g->promotion_to==0);
    click(&c,g,1,"promote","n"); CHECK(g->pos.board[0]==KNIGHT && !g->pos.board[8]);

    g=new_game(&c); click(&c,g,2,"join","b");
    g->pos=fen("7k/8/8/8/8/8/8/R5K1 w - - 100 1");
    click(&c,g,2,"claim",NULL); CHECK(g->state==PLAYING);
    click(&c,g,1,"claim",NULL); CHECK(g->state==FINISHED && g->reason==FIFTY);
    g=new_game(&c); click(&c,g,3,"close",NULL); CHECK(g->state==LOBBY);
    c.uid=3; c.args=g->id; cmd_chessend(&c,&api); CHECK(g->state==LOBBY);
    c.uid=1; cmd_chessend(&c,&api); CHECK(g->state==EMPTY);

    Callback cb;
    CHECK(!parse_callback("chess:",&cb)); CHECK(!parse_callback("chess:12345678:1:sq:-1:extra",&cb));
    CHECK(!parse_callback("chess:12345678:99999999999999999999:sq:1",&cb));
    CHECK(!parse_callback("chess:12345678::sq:1",&cb));
    rev=g->revision;
    click(&c,g,1,"sq","-1"); click(&c,g,1,"sq","64"); click(&c,g,1,"sq","9999999999999999999999");
    CHECK(g->revision==rev);
    for (int i=0; i<MAX_GAMES; ++i) games[i].state=PLAYING;
    CHECK(allocate_game()==NULL);
    games[15].state=FINISHED; CHECK(allocate_game()==&games[15]);
    printf("Rules + callback/UI checks: %d passed\n",checks);
}

int main(void) {
    rules_tests(); ui_tests();
    const koto_module* module=koto_module_register();
    CHECK(module->abi_version==3 && !strcmp(module->name,"chess") && module->command_count==4 && module->settings_count==0);
    printf("All %d checks passed.\n",checks);
    return 0;
}

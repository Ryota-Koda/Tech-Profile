//-----------------------------------------------------------------
//カーナビプログラム
//-----------------------------------------------------------------

#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <GL/glfw.h>
#include <FTGL/ftgl.h>
#include <string.h>
#include <time.h>

#define MaxCross 100        /* 最大交差点数=100 */
#define MaxName  50         /* 最大文字数50文字(半角) */

#define PATH_SIZE     100   /* 経路上の最大の交差点数 */
#define MARKER_RADIUS 0.1   /* マーカーの半径 */

/* 座標変換マクロの定義 */
double ORIGIN_X;
double ORIGIN_Y;

//ウィンドウの表示速度の変数
int window_speed = 100;

#ifndef FONT_FILENAME
/* フォントのファイル名 */
#define FONT_FILENAME "/usr/share/fonts/truetype/takao-gothic/TakaoGothic.ttf"
#endif
static FTGLfont *font; /* 読み込んだフォントを差すポインタ */

//交差点の構造体(位置)
typedef struct {
    double x, y;            /* 位置 x, y */
} Position;                 /* 位置を表す構造体 */
//交差点の構造体(全部)
typedef struct {
    int id;                 /* 交差点番号 */
    Position pos;           /* 位置を表す構造体 */
    double wait;            /* 平均待ち時間 */
    char jname[MaxName];    /* 交差点名(日本語) */
    char ename[MaxName];    /* 交差点名(ローマ字) */
    int points;             /* 交差道路数 */
    int next[5];            /* 隣接する交差点番号 */
    double distance;        /* 基準交差点からのトータル距離：追加 */
    double time;            /* 基準交差点からのトータル時間 */
    int previous_distance;           /* 基準交差点からの経路（直前の交差点番号）：追加 */
    int previous_time;
} Crossing;

//交差点情報の配列と経路の配列の定義
static Crossing cross[MaxCross];

//ファイルを読み込む関数
static int map_read(char *filename) {
    FILE *fp;
    int i, j;
    int crossing_number;          /* 交差点数 */

    fp = fopen(filename, "r");
    if (fp == NULL) {
        perror(filename);
        return -1;
    }

    /* はじめに交差点数を読み込む */
    fscanf(fp, "%d", &crossing_number);

    for (i = 0; i < crossing_number; i++) {

        fscanf(fp, "%d,%lf,%lf,%lf,%[^,],%[^,],%d",
                     &(cross[i].id), &(cross[i].pos.x), &(cross[i].pos.y),
                     &(cross[i].wait), cross[i].jname,
                     cross[i].ename, &(cross[i].points));

         for(j=0; j < 5; ++j){
        cross[i].next[j] = -1; 
    }

        for (j = 0; j < cross[i].points; j++) {
            fscanf(fp, ",%d", &(cross[i].next[j]));
        }

    }
    fclose(fp);

    return crossing_number;
}

//円を描く関数
static void draw_circle(double x, double y, double r) {
    int const N = 24;             /* 円周を 24分割して線分で描画することにする */
    int i;

    glBegin(GL_LINE_LOOP);
    for (i = 0; i < N; i++)
        glVertex2d(x + cos(2 * M_PI * i / N) * r,
                   y + sin(2 * M_PI * i / N) * r);
    glEnd();
}

//球を描く関数
static void draw_ball(double x, double y, double r){
    int const N = 6;
    int i ;
    for(i = 0; i < N; ++i){
        glPushMatrix();

        glTranslated(x, y, 0);
        glRotatef(90, 1.0, 0, 0);
        glRotatef(360 * i / N, 0, 1.0, 0);
        glTranslated(-x, -y, 0);
        draw_circle(x, y, r);

        glTranslated(x, y, 0);
        glRotatef(90, 0, 0, 0);
        glRotatef(360 * i / N, 1.0, 0, 0);
        glTranslated(-x, -y, 0);
        draw_circle(x, y, r);

        glTranslated(x, y, 0);
        glRotatef(90, 0, 0, 0);
        glRotatef(360 * i / N, 0, 1.0, 0);
        glTranslated(-x, -y, 0);
        draw_circle(x, y, r);

        glPopMatrix();
    }
}

//円錐を描く関数
static void draw_corn(double x, double y, double z, double r){
    int const N = 12;
    int i;
    //底面
    glNormal3d(0.0,-1.0,0.0);
    glBegin(GL_TRIANGLE_FAN);
    for (i = 0; i < N; i++){
        glVertex3d(x + cos(2 * M_PI * i / N) * r, y + sin(2 * M_PI * i / N) * r,0);
    }
    glEnd();
    //側面
    for (i = 0; i < N ; i++){
        glBegin(GL_TRIANGLES);
        glVertex3d(x,y,z);
        glVertex3d(x + cos(2 * M_PI * i / N) * r, y + sin(2 * M_PI * i / N) * r,0);
        glVertex3d(x + cos(2 * M_PI * (i+1) / N) * r, y + sin(2 * M_PI * (i+1) / N) * r,0);
        glEnd();
    }
}

//文字列を描く関数
static void draw_outtextxy(double x, double y, char const *text, double rotation, double rotation_z) {
    double const scale = 0.01;
    glPushMatrix();
    glTranslated(x, y, 0.0);
    glRotatef(rotation,0,0,1.0);
    glRotatef(rotation_z,1.0,0,0);
    glScaled(scale, scale, scale);
    ftglRenderFont(font, text, FTGL_RENDER_ALL);
    glPopMatrix();
}

//道路網を描く関数
static void map_show(int crossing_number) {
    int i, j;
    double x0, y0, x1, y1, x2, y2;

    for (i = 0; i < crossing_number; i++) {     /* 交差点毎のループ */
        x0 = cross[i].pos.x;
        y0 = cross[i].pos.y;

        /* 交差点を表す円を描く */
        glColor3d(1.0, 0.5, 0.5);
        draw_corn(x0,y0,0.3,0.05);


        /* 交差点から伸びる道路を描く */
        glColor3d(1.0, 1.0, 1.0);
        for (j = 0; j < cross[i].points; j++) {
            x1 = cross[ cross[i].next[j] ].pos.x;
            y1 = cross[ cross[i].next[j] ].pos.y;
            x2 = (x0 + x1)/2;
            y2 = (y0 + y1)/2; //中間点の設定

            glBegin(GL_LINES);
            glColor3d(1,0,0);
            glVertex2d(x0, y0);
            glColor3d(1,1,1);
            glVertex2d(x2, y2);
            glEnd();

            glBegin(GL_LINES);
            glColor3d(1,1,1);
            glVertex2d(x2,y2);
            glColor3d(1,0,0);
            glVertex2d(x1,y1);
            glEnd();

        }
    }
}

//経路の始点と終点の交差点名を表示する関数
static void draw_intersection_name(int vehicle_pathIterator, int path[], double rotation, double rotation_z){
    double x0, y0, x1, y1;
    x0 = cross[path[vehicle_pathIterator + 0]].pos.x;
    y0 = cross[path[vehicle_pathIterator + 0]].pos.y;
    x1 = cross[path[vehicle_pathIterator + 1]].pos.x;
    y1 = cross[path[vehicle_pathIterator + 1]].pos.y;
    glColor3d(1.0,1.0,0.0);
    draw_outtextxy(x0, y0, cross[path[vehicle_pathIterator + 0]].jname, rotation, rotation_z);
    if(path[vehicle_pathIterator + 1] != -1){
        draw_outtextxy(x1, y1, cross[path[vehicle_pathIterator + 1]].jname, rotation, rotation_z);
    } 
}
//経路上の交差点名をすべて表示する関数
static void draw_intersection_pathname(int path[], double rotation, double rotation_z){
    int i = 0;
    double x0,y0;
    while(1){
        if(path[i] == -1){
            break;
        }
        x0 = cross[path[i]].pos.x;
        y0 = cross[path[i]].pos.y;

        /* 交差点の名前を描く */
        glColor3d(1.0, 1.0, 0.0);
        draw_outtextxy(x0, y0, cross[path[i]].jname, rotation, rotation_z);

        i++;
    }
}
//交差点名をすべて表示する関数
static void draw_intersection_allname(int crossing_number, double rotation, double rotation_z){
    int i;
    double x0,y0;
    for(i = 0; i < crossing_number; ++i){
        x0 = cross[i].pos.x;
        y0 = cross[i].pos.y;

        /* 交差点の名前を描く */
        glColor3d(1.0, 1.0, 0.0);
        draw_outtextxy(x0, y0, cross[i].jname, rotation, rotation_z);
    }
}

//メイン経路を表示
static void draw_main_path(int path[],int choice_mode){
    int i =0;
    double x0, x1, y0, y1;
    while(1){
        if(path[i+1] == -1){
            break;
        }
        x0 = cross[ path[i] ].pos.x;
        y0 = cross[ path[i] ].pos.y;
        x1 = cross[ path[i+1] ].pos.x;
        y1 = cross[ path[i+1] ].pos.y;
        glLineWidth(6.0);
        glBegin(GL_LINES);
        if(choice_mode == 0){
            glColor3d(0,0,1);
        }
        else if(choice_mode == 1){
            glColor3d(0.6,1.0,0.2);
        }
        
        glVertex2d(x0,y0);
        glVertex2d(x1,y1);
        glEnd();
        glLineWidth(1.0);
        i++;
    }
}
//サブ経路を表示
static void draw_sub_path(int path[],int choice_mode){
    int i =0;
    double x0, x1, y0, y1;
    while(1){
        if(path[i+1] == -1){
            break;
        }
        x0 = cross[ path[i] ].pos.x;
        y0 = cross[ path[i] ].pos.y;
        x1 = cross[ path[i+1] ].pos.x;
        y1 = cross[ path[i+1] ].pos.y;
        glLineWidth(1.5);
        glBegin(GL_LINES);
        if(choice_mode == 1){
            glColor3d(0,0,1);
        }
        else if(choice_mode == 0){
            glColor3d(0.6,1.0,0.2);
        }
        
        glVertex2d(x0,y0);
        glVertex2d(x1,y1);
        glEnd();
        glLineWidth(1.0);
        i++;
    }
}

//交差点間の距離を計算
double distance(int a, int b){
  return hypot(cross[a].pos.x-cross[b].pos.x,
	       cross[a].pos.y-cross[b].pos.y);
}

//ダイクストラ法(距離)による目的地からの最短距離算出
void dijkstra_distance(int crossing_number,int target){
  int i,j,n;
  double min_distance;
  double d;
  int min_cross = 0;
  int done[MaxCross];     /* 確定済み:1 未確定:0 を入れるフラグ */

  for(i=0;i<crossing_number;i++)/* 初期化 */
    {
      cross[i].distance=1e100;  /* 初期値は有り得ないくらい大きな値 */
      cross[i].previous_distance=-1;     /* 最短経路情報を初期化 */
      done[i]=0;                /* 全交差点未確定 */
    }

  /* ただし、基準の交差点は 0 */
  cross[target].distance=0;
  
  for(i=0;i<crossing_number;i++) /* c_number回やれば終わるはず */
    {
      /* 最も距離数値の小さな未確定交差点を選定 */
      min_distance=1e100;
      for(j=0;j<crossing_number;j++)
	{  /* _未確定？_    ______暫定最短より近い？_______ */
	  if((done[j]==0)&&(cross[j].distance < min_distance))
	    {
	      min_distance=cross[j].distance;
	      min_cross=j;
	    }
	}
      /* 交差点 min_cross は 確定できる */
      done[min_cross]=1;  /* 確定 */
      /* 確定交差点周りで距離の計算 */
      for(j=0;j<cross[min_cross].points;j++)
	{
	  n=cross[min_cross].next[j];    /* 長ったらしいので置き換え(だけ) */
	  /* 評価指標 */
	  d=distance(min_cross,n)+cross[min_cross].distance;
	  /* 現在の暫定値と比較して、短いなら更新 */
	  if(cross[n].distance > d){
	    cross[n].distance = d;
	    cross[n].previous_distance = min_cross;
	  }
	}
    }
}

//ダイクストラ法(時間)による目的地への最短時間導出
void dijkstra_time(int crossing_number, int target, double speed){
    int i,j,n;
    double min_time;
    double t;
    int min_cross = 0;
    int done[MaxCross];  //確定済み1　未確定0を入れるフラグ

    for(i=0;i<crossing_number;i++){     /* 初期化 */
      cross[i].time=1e100;  /* 初期値は有り得ないくらい大きな値 */
      cross[i].previous_time=-1;     /* 最短経路情報を初期化 */
      done[i]=0;                /* 全交差点未確定 */
    }

    //ただし基準の交差点は0
    cross[target].time = 0;
    
    for(i = 0; i < crossing_number; ++i){   //c_number回で終わるはず
        //最も時間数値の未確認交差点を選定
        min_time = 1e100;
        for(j = 0; j < crossing_number; ++j){
            if((done[j] == 0) && (cross[j].time < min_time)){
                min_time = cross[j].time;
                min_cross = j;
            }
        }
        //交差点min_crossは確定できる
        done[min_cross] = 1;
        //確定交差点周りで距離の計算
        for(j = 0; j < cross[min_cross].points; ++j){
            n = cross[min_cross].next[j];
            //評価指標(隣接交差点の待ち時間　+　交差点に行くまでの時間)
            t = cross[n].wait + (distance(min_cross,n)/(speed/60)) + cross[min_cross].time;
            //現在の暫定値と比較して、短いなら更新
            if(cross[n].time > t){
                cross[n].time = t;
                cross[n].previous_time = min_cross;
            }
        }
    }
}

//最短経路計算
int pickup_path_distance(int crossing_number,int start,int goal,int path[],int maxpath){
  int c=start;         /* 現在いる交差点 */
  int i;

  path[0]=start;
  i=1;
  c=start;             /* 現在値を start に設定 */
  while(c!=goal)
    {
      c=cross[c].previous_distance;
      path[i]=c;
      i++;
    }
  return 0;
}
//最短時間計算
int pickup_path_time(int crossing_number,int start,int goal,int path[],int maxpath){
  int c=start;         /* 現在いる交差点 */
  int i;

  path[0]=start;
  i=1;
  c=start;             /* 現在値を start に設定 */
  while(c!=goal)
    {
      c=cross[c].previous_time;
      path[i]=c;
      i++;
    }
  return 0;
}

//合計距離計算
double calculate_distance(int path[]){
    int i = 0;
    double all_distance = 0.0;
    while(1){
        if(path[i+1] == -1){
            break;
        }
        all_distance = all_distance + distance(path[i],path[i+1]);
        i++;
    }
    return all_distance;
}
//合計時間計算
double calculate_time(int path[],double speed){
    int i = 0;
    double all_time = 0.0;
    while(1){
        if(path[i+1] == -1){
            break;
        }
        all_time = all_time + cross[path[i]].wait + distance(path[i],path[i+1]) / (speed/60);
        i++;
    }
    //現在地の交差点の待ち時間は考慮しないものとする
    all_time = all_time - cross[path[0]].wait;

    return all_time;
}
//経路をリセットする関数
int path_reset(int path[], int pathmax){
    int i;
    for(i = 0; i < pathmax; ++i){
        path[i] = -1;
    }
    return 0;
}

//経路から交差点の座標を導く関数
double id_to_posx(int path[], int id){
    
    return cross[ path[id] ].pos.x;
}
double id_to_posy(int path[], int id){
    
    return cross[ path[id] ].pos.y;
}

//交差点を検索する関数(日本語)
int search_cross_ja(int num){
    int i,k;
    int j = 1;
    int f = -1;
    char input[200];
    int output[MaxCross];
    //outputを初期化
    for(i = 0; i < MaxCross; ++i){
        output[i] = -1;
    }
    printf("交差点名を入力してください(日本語)\n");
    scanf("%s",input);
    puts("");
    //まず完全一致から探す
    for(i = 0; i < num; ++i){
        if(strcmp(cross[i].jname,input) == 0){
            f = i;          //見つけた番号保持
            goto searchend;
        }
    }
    //次に部分一致を探す
    for(i = 0; i < num; ++i){
        if(strstr(cross[i].jname, input)!=NULL){
            output[j] = i;  //見つけた番号を配列に保存
            j++;
        }
    }
    if(j == 1){
        goto searchend;
    }
    k = j;
    j = 1;
    //部分一致の候補表示
    printf("'%s'が含まれる交差点を表示します\n",input);
    for(i = 1; i < k; ++i){
        printf("%d. %s\n",i,cross[output[j]].jname);
        j++;
    }
    printf("交差点を選択してください(数字)\n");
    printf("input>");
    scanf("%d",&i);
    if (i >= 0 && i <= j){
        f = output[i];
    }
    searchend:
    if(f == -1){
        printf("交差点を見つけることができませんでした\n");
    }
    return f;
}

//交差点を検索する関数(英語)
int search_cross_en(int num){
    int i,k;
    int j = 1;
    int f = -1;
    char input[200];
    int output[MaxCross];
    //outputを初期化
    for(i = 0; i < MaxCross; ++i){
        output[i] = -1;
    }
    printf("交差点名を入力してください(英語)\n");
    scanf("%s",input);
    puts("");
    //まず完全一致から探す
    for(i = 0; i < num; ++i){
        if(strcmp(cross[i].ename,input) == 0){
            f = i;          //見つけた番号保持
            goto searchend;
        }
    }
    //次に部分一致を探す
    for(i = 0; i < num; ++i){
        if(strstr(cross[i].ename, input)!=NULL){
            output[j] = i;  //見つけた番号を配列に保存
            j++;
        }
    }
    if(j == 1){
        goto searchend;
    }
    k = j;
    j = 1;
    //部分一致の候補表示
    printf("'%s'が含まれる交差点を表示します\n",input);
    for(i = 1; i < k; ++i){
        printf("%d. %s\n",i,cross[output[j]].ename);
        j++;
    }
    printf("交差点を選択してください(数字)\n");
    printf("input>");
    scanf("%d",&i);
    if (i >= 0 && i <= j){
        f = output[i];
    }
    searchend:
    if(f == -1){
        printf("交差点を見つけることができませんでした\n");
    }
    return f;
}

//交差点を検索する関数(交差点ID)
int search_cross_id(int num){
    int input;
    int f = -1;
    printf("交差点名を入力してください(ID)\n");
    printf("input>");
    scanf("%d",&input);
    puts("");
    if(0 <= input && input < num){
        f = input;
    }
    if(f == -1){
        printf("交差点を見つけることができませんでした\n");
    }
    return f;
}

//メイン
int main(void){
    int crossing_number;        //合計交差点数
    int goal,start;             //現在地＆目的地
    int path[PATH_SIZE], path_sub[PATH_SIZE];         //経路の配列
    int i,j=0;
    double map_x = 0.0, map_y = 0.0; //地図をどれだけ動かすかの座標
    int steps;
    double rotation = 0,rotation_step;
    int vehicle_pathIterator;     /* 移動体の経路上の位置 (何個目の道路か) */
    int vehicle_stepOnEdge;       /* 移動体の道路上の位置 (何ステップ目か) */
    int vehicle_steprotation; //移動体の交差点での回転(何ステップ目か)
    int width, height;
    double x0,y0,x1,y1,x2,y2;
    double distance,norm1,norm2,dot_product,cross_product,rad,deg;    //移動距離や回転量を計算するのに使う変数
    int mode = 0; //0では回転、1では移動,2で移動のみを行う合図
    int cheak = 0; //mode の値を保存する変数
    int mode2 = 0;  //ゴールにたどり着くか判断
    int choice;   // 選択肢の変数
    double range_x = 0.0, range_y = 0.0, range_z = 1.5; //透視投影の距離 
    double rotation_x = 0,rotation_z = 0; //透視投影の見る角度
    double speed = 30.0,pre_speed;  //車の速度
    int choice_mode = 0; //最短距離0、最短時間1の変数
    int word_mode = 0; //文字の表示方法を変える変数 
    double all_distance, all_time; //経路の合計距離と合計時間
    int wait_time; //目的地に着いた時の待ち時間

    //マップファイルの読み込み
    crossing_number = map_read("map2.dat");
    if (crossing_number < 0) {
        fprintf(stderr, "couldn't read map file\n");
        exit(1);
    }
    //適当に初期化
    for(i=0;i<crossing_number;i++){
        cross[i].distance=0;    
        cross[i].time = 0;      
        cross[i].previous_distance=-1;    
        cross[i].previous_time=-1;    
    }

    //--------------------------カーナビ開始---------------------------
    printf("\nカーナビ起動\n\n");

    while(1){                                //目的地入力、もう一度やり直すためのループ
        step1:
        printf("現在地、目的地をどのように設定しますか\n");
        printf("1.日本語 2. ローマ字 3. 交差点ID 4. ランダム 5.車の速度を変更\n");
        printf("input>");

        scanf("%d",&choice);

        //現在地、目的地の検索、設定
        if(choice == 1){
            printf("現在地を入力します\n");
            start = search_cross_ja(crossing_number);
            if(start == -1){
                goto step1;
            }
            printf("現在地を'%s  %s'と設定します\n",cross[start].jname,cross[start].ename);

            printf("目的地を入力します\n");
            goal = search_cross_ja(crossing_number);
            if(goal == -1){
                goto step1;
            }
            printf("目的地を'%s  %s'と設定します\n",cross[goal].jname,cross[goal].ename);
            if(start == goal){
                printf("現在地と目的地が同じです。設定しなおしてください\n");
                goto step1;
            }
        }
        else if(choice == 2){
            printf("現在地を入力します\n");
            start = search_cross_en(crossing_number);
            if(start == -1){
                goto step1;
            }
            printf("現在地を'%s  %s'と設定します\n",cross[start].jname,cross[start].ename);

            printf("目的地を入力します\n");
            goal = search_cross_en(crossing_number);
            if(goal == -1){
                goto step1;
            }
            printf("目的地を'%s  %s'と設定します\n",cross[goal].jname,cross[goal].ename);
            if(start == goal){
                printf("現在地と目的地が同じです。設定しなおしてください\n");
                goto step1;
            }
        }
        else if(choice == 3){
            printf("現在地を入力します\n");
            start = search_cross_id(crossing_number);
            if(start == -1){
                goto step1;
            }
            printf("現在地を'%s  %s'と設定します\n",cross[start].jname,cross[start].ename);

            printf("目的地を入力します\n");
            goal = search_cross_id(crossing_number);
            if(goal == -1){
                goto step1;
            }
            printf("目的地を'%s  %s'と設定します\n",cross[goal].jname,cross[goal].ename);
            if(start == goal){
                printf("現在地と目的地が同じです。設定しなおしてください\n");
                goto step1;
            }
        }            
        else if(choice == 4){
            srand(time(NULL));
            start = rand() % crossing_number;
            while(1){
                goal = rand() % crossing_number;
                if (goal != start && goal != cross[start].next[0] && goal != cross[start].next[1] && goal != cross[start].next[2] && goal != cross[start].next[3] && goal != cross[start].next[4]) {
                    break;
                }
            }
            printf("現在地を'%s  %s'と設定します\n",cross[start].jname,cross[start].ename);
            printf("目的地を'%s  %s'と設定します\n",cross[goal].jname,cross[goal].ename);
        }
        else if(choice == 5){
            printf("現在の車の速度は'%.1lf'km/hです。\n",speed);
            printf("車の速度を入力してください。\n");
            printf("input>");
            scanf("%lf",&pre_speed);
            if(pre_speed <= 0){
                printf("その速度は入力できません。\n");
                goto step1;
            }
            else{
                speed = pre_speed;
                printf("車の速度を'%.1lf'km/hに設定しました。\n",speed);
                goto step1;
            }
        }
        else{
            printf("無効な入力です\n");
            goto step1;
        }

        //初回経路リセット
        path_reset(path, PATH_SIZE);
        path_reset(path_sub, PATH_SIZE);

        //ダイクストラ法を行う
        dijkstra_distance(crossing_number,goal);
        dijkstra_time(crossing_number,goal,speed);
            
        //経路の決定(pathが決まる)
        if(pickup_path_distance(crossing_number,start,goal,path,20)<0){
            return 1;    
        }
        //経路の決定(path_subが決まる)
        if(pickup_path_time(crossing_number,start,goal,path_sub,20)<0){
            return 1;
        }

        //最短経路の合計時間と合計距離
        all_distance = calculate_distance(path);
        all_time = calculate_time(path,speed);
        printf("\n");
        printf("車の速度'%.1lf'km/h\n",speed);
        printf("\n");

        printf("最短経路(青)\n");
        printf("目的地までの距離: %.2lfkm   目的地までの所要時間: %.2lf分\n",all_distance,all_time);

        //最短時間の合計時間と合計距離
        all_distance = calculate_distance(path_sub);
        all_time = calculate_time(path_sub,speed);

        printf("最短経路(黄緑)\n");
        printf("目的地までの距離: %.2lfkm   目的地までの所要時間: %.2lf分\n",all_distance,all_time);
            

        printf("\n");
        printf("---操作方法-----------------------------------------\n");
        printf("Escキーでウィンドウを閉じる\n");
        printf("SPACEキーで一時停止\n");
        printf("WASDで上下左右に視点を移動\n");
        printf("Eで視点が上昇、Qで視点が降下\n");
        printf("Rで視点を初期状態にリセット\n");
        printf("上下左右方向キーで視点の角度を変更\n");
        printf("Mでマップの回転の有無を変更\n");
        printf("Pで最短距離経路(青)と最短時間経路を変更(黄緑)\n");
        printf("Bで交差点の表示方法を変更(3通り)\n");
        printf("----------------------------------------------------\n");

        sleep(1);

        step2:

        /* グラフィック環境を初期化して、ウィンドウを開く */
        glfwInit();
        glfwOpenWindow(1000, 800, 0, 0, 0, 0, 0, 0, GLFW_WINDOW);
        
        while(1){
            /* Esc が押されるかウィンドウが閉じられたらおしまい */
            if (glfwGetKey(GLFW_KEY_ESC) || !glfwGetWindowParam(GLFW_OPENED)){
                goto loopend;
            }
            //もしRキーが押されたら、移動情報をリセット
            if(glfwGetKey(82)){
                range_x = 0; range_y = 0; range_z = 1.5;
                rotation_x = 0; rotation_z = 0;
            }
            //もしWキーが押されたら前に移動
            if(glfwGetKey(87)){
                range_y = range_y + 0.5 * cos((rotation+rotation_x) * M_PI / 180);
                range_x = range_x + 0.5 * sin((rotation+rotation_x) * M_PI / 180);
            }
            //もしSキーが押されたら後ろに移動
            if(glfwGetKey(83)){
                range_y = range_y + 0.5 * cos((rotation+rotation_x) * M_PI / 180 + M_PI);
                range_x = range_x + 0.5 * sin((rotation+rotation_x) * M_PI / 180 + M_PI);
            }
            //もしDキーが押されたら右に移動
            if(glfwGetKey(68)){
                range_y = range_y + 0.5 * cos((rotation+rotation_x) * M_PI / 180 + M_PI / 2);
                range_x = range_x + 0.5 * sin((rotation+rotation_x) * M_PI / 180 + M_PI / 2);
            }
            //もしAキーが押されたら左に移動
            if(glfwGetKey(65)){
                range_y = range_y + 0.5 * cos((rotation+rotation_x) * M_PI / 180 + 3 * M_PI / 2);
                range_x = range_x + 0.5 * sin((rotation+rotation_x) * M_PI / 180 + 3 * M_PI / 2);
            }
            //もしEキーが押されたら透視投影の距離を遠くする
            if(glfwGetKey(69)){
                range_z = range_z + 0.5;
            }
            //もしQキーが押されたら透視投影の距離を近くする
            if(glfwGetKey(81)){
                if(range_z >= 1.0){
                        range_z = range_z - 0.5;
                }
            }
            //もし上キーが押されたら透視投影の角度を上
            if(glfwGetKey(GLFW_KEY_UP)){
                if(rotation_z > 10) {
                    rotation_z = rotation_z - 10;
                }
            }
            //もし下キーが押されたら透視投影の角度を下
            if(glfwGetKey(GLFW_KEY_DOWN)){
                if(rotation_z < 90){
                    rotation_z = rotation_z + 10;
                }
            }  
            //もし右キーが押されたら透視投影の角度を時計回り
            if(glfwGetKey(GLFW_KEY_RIGHT)){
                rotation_x = rotation_x + 10;
            }
            //もし左キーが押されたら透視投影の角度を反時計回り
            if(glfwGetKey(GLFW_KEY_LEFT)){
                rotation_x = rotation_x - 10;
            }                        
            //もしMキーが押されたらマップの回転の有無を変更
            if(glfwGetKey(77)){
                if(mode != 2){
                    mode = 2;
                }
                else{
                    mode = 0;
                }
                range_x = 0; range_y = 0; range_z = 1.5;
                rotation_x = 0; rotation_z = 0;
                glfwTerminate();
                goto step2;
                
            }
            //もしPキーが押されたら最短距離と最短経路を変更
            if(glfwGetKey(80)){
                if(choice_mode == 0){
                    choice_mode = 1;
                }
                else{
                    choice_mode = 0;
                }
                range_x = 0; range_y = 0; range_z = 1.5;
                rotation_x = 0; rotation_z = 0;
                glfwTerminate();
                goto step2;
            }
            //もしBキーが押されたら交差点の表示方法を変更する
            if(glfwGetKey(66)){
            if(word_mode == 0){
                    word_mode = 1;
                    window_speed = 125;
                }
                else if (word_mode == 1){
                    word_mode = 2;
                    window_speed = 150;
                }
                else if (word_mode == 2){
                    word_mode = 0;
                    window_speed = 100;
                }
            }
            //もしSPACEキーが押されたら、一時停止
            if(glfwGetKey(GLFW_KEY_SPACE)){
                if(mode != 3){
                    cheak = mode;
                    mode = 3;
                }
                else{
                    mode = cheak;
                }
            }

            //初回経路リセット
            path_reset(path, PATH_SIZE);
            path_reset(path_sub, PATH_SIZE);
            rotation = 0;

            //ダイクストラ法を行う
            dijkstra_distance(crossing_number,goal);
            dijkstra_time(crossing_number,goal,speed);
            //経路の決定
            if(choice_mode == 0){
                //経路の決定(pathが決まる)
                if(pickup_path_distance(crossing_number,start,goal,path,20)<0){
                    return 1;
                }
                //経路の決定(path_subが決まる)
                if(pickup_path_time(crossing_number,start,goal,path_sub,20)<0){
                    return 1;
                }
            }
            else if(choice_mode == 1){
                //経路の決定(pathが決まる)
                if(pickup_path_time(crossing_number,start,goal,path,20)<0){
                    return 1;
                }
                 //経路の決定(pathが決まる)
                if(pickup_path_distance(crossing_number,start,goal,path_sub,20)<0){
                    return 1;
                }
            }
            
            //地図の最初の中心を決める
            ORIGIN_X = cross[path[0]].pos.x;
            ORIGIN_Y = cross[path[0]].pos.y;

            //移動体の位置を初期化
            vehicle_pathIterator = 0;
            vehicle_stepOnEdge = 0;
            vehicle_steprotation = 0;

            //初回スイッチリセット
            if(mode != 2){
                mode = 0;
            }
            
            //ウィンドウ作成＆アニメーションの実行
            while(1){
                /* Esc が押されるかウィンドウが閉じられたらおしまい */
                if (glfwGetKey(GLFW_KEY_ESC) || !glfwGetWindowParam(GLFW_OPENED)){
                    goto loopend;
                }
                //もしRキーが押されたら、移動情報をリセット
                if(glfwGetKey(82)){
                    range_x = 0; range_y = 0; range_z = 1.5;
                    rotation_x = 0; rotation_z = 0;
                }
                //もしWキーが押されたら前に移動
                if(glfwGetKey(87)){
                    range_y = range_y + 0.5 * cos((rotation+rotation_x) * M_PI / 180);
                    range_x = range_x + 0.5 * sin((rotation+rotation_x) * M_PI / 180);
                }
                //もしSキーが押されたら後ろに移動
                if(glfwGetKey(83)){
                    range_y = range_y + 0.5 * cos((rotation+rotation_x) * M_PI / 180 + M_PI);
                    range_x = range_x + 0.5 * sin((rotation+rotation_x) * M_PI / 180 + M_PI);
                }
                //もしDキーが押されたら右に移動
                if(glfwGetKey(68)){
                    range_y = range_y + 0.5 * cos((rotation+rotation_x) * M_PI / 180 + M_PI / 2);
                    range_x = range_x + 0.5 * sin((rotation+rotation_x) * M_PI / 180 + M_PI / 2);
                }
                //もしAキーが押されたら左に移動
                if(glfwGetKey(65)){
                    range_y = range_y + 0.5 * cos((rotation+rotation_x) * M_PI / 180 + 3 * M_PI / 2);
                    range_x = range_x + 0.5 * sin((rotation+rotation_x) * M_PI / 180 + 3 * M_PI / 2);
                }
                //もしEキーが押されたら透視投影の距離を遠くする
                if(glfwGetKey(69)){
                    range_z = range_z + 0.5;
                }
                //もしQキーが押されたら透視投影の距離を近くする
                if(glfwGetKey(81)){
                    if(range_z >= 1.0){
                        range_z = range_z - 0.5;
                    }
                }
                //もし上キーが押されたら透視投影の角度を上
                if(glfwGetKey(GLFW_KEY_UP)){
                    if(rotation_z > 10) {
                        rotation_z = rotation_z - 10;
                    }
                }
                //もし下キーが押されたら透視投影の角度を下
                if(glfwGetKey(GLFW_KEY_DOWN)){
                    if(rotation_z < 90){
                        rotation_z = rotation_z + 10;
                    }
                }
                //もし右キーが押されたら透視投影の角度を時計回り
                if(glfwGetKey(GLFW_KEY_RIGHT)){
                    rotation_x = rotation_x + 10;
                }
                //もし左キーが押されたら透視投影の角度を反時計回り
                if(glfwGetKey(GLFW_KEY_LEFT)){
                    rotation_x = rotation_x - 10;
                }
                //もしMキーが押されたらマップの回転の有無を変更
                if(glfwGetKey(77)){
                    if(mode != 2){
                        mode = 2;
                    }
                    else{
                        mode = 0;
                    }
                    range_x = 0; range_y = 0; range_z = 1.5;
                    rotation_x = 0; rotation_z = 0;
                    glfwTerminate();
                    goto step2;
                }
                //もしPキーが押されたら最短距離と最短経路を変更
                if(glfwGetKey(80)){
                    if(choice_mode == 0){
                        choice_mode = 1;
                    }
                    else{
                        choice_mode = 0;
                    }
                    range_x = 0; range_y = 0; range_z = 1.5;
                    rotation_x = 0; rotation_z = 0;
                    glfwTerminate();
                    goto step2;
                }
                //もしBキーが押されたら交差点の表示方法を変更する
                if(glfwGetKey(66)){
                    if(word_mode == 0){
                        word_mode = 1;
                        window_speed = 125;
                    }
                    else if (word_mode == 1){
                        word_mode = 2;
                        window_speed = 150;
                    }
                    else if (word_mode == 2){
                        word_mode = 0;
                        window_speed = 100;
                    }
                }
                //もしSPACEキーが押されたら、一時停止
                if(glfwGetKey(GLFW_KEY_SPACE)){
                    if(mode != 3){
                        cheak = mode;
                        mode = 3;
                    }
                    else{
                        mode = cheak;
                    }
                }

                /* (ORIGIN_X, ORIGIN_Y) を中心に、REAL_SIZE_X * REAL_SIZE_Y の範囲の空間をビューポートに投影する */
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                gluPerspective(120.0,1.0,0,50);

                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glRotatef(-rotation_z,1.0,0,0);
                glRotatef(rotation + rotation_x,0,0,1.0);                
                glTranslated(-range_x,-range_y,-range_z);
                glTranslated(-ORIGIN_X,-ORIGIN_Y,0);

                /* 文字列描画のためのフォントの読み込みと設定 */
                font = ftglCreateExtrudeFont(FONT_FILENAME);
                if (font == NULL) {
                    perror(FONT_FILENAME);
                    fprintf(stderr, "could not load font\n");
                    exit(1);
                }
                ftglSetFontFaceSize(font, 24, 24);
                ftglSetFontDepth(font, 0.01);
                ftglSetFontOutset(font, 0, 0.1);
                ftglSetFontCharMap(font, ft_encoding_unicode);
                   
                glfwGetWindowSize(&width, &height); /* 現在のウィンドウサイズを取得する */
                glViewport(0, 0, width, height); /* ウィンドウ全面をビューポートにする */

                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT); /* バックバッファを黒で塗り潰す */

                map_show(crossing_number);                /* 道路網の表示 */
                draw_main_path(path,choice_mode);
                draw_sub_path(path_sub,choice_mode);
                glColor3d(0.6,1.0,1.0);                   //現在地と目的地の表示
                draw_corn(cross[start].pos.x,cross[start].pos.y,0.4,0.05);
                draw_corn(cross[goal].pos.x,cross[goal].pos.y,0.4,0.05);

                switch(mode){
                    case 0:                             //回転を行う
                  
                        if (path[vehicle_pathIterator + 0] != -1 && path[vehicle_pathIterator + 1] != -1){
                            //------------------回転量の計算----------------------------
                            x1 = id_to_posx(path,vehicle_pathIterator + 0);//今の交差点
                            x2 = id_to_posx(path,vehicle_pathIterator + 1);//次の交差点
                            y1 = id_to_posy(path,vehicle_pathIterator + 0);//今の交差点
                            y2 = id_to_posy(path,vehicle_pathIterator + 1);//次の交差点
                            if(vehicle_pathIterator == 0){
                                x0 = x1;
                                y0 = y1 -1;
                            }
                            else{
                                x0 = id_to_posx(path,vehicle_pathIterator - 1);//前の交差点
                                y0 = id_to_posy(path,vehicle_pathIterator - 1);//前の交差点                            
                            }

                            dot_product = (x1 - x0)*(x1 - x2) + (y1 - y0)*(y1 - y2); //内積の計算
                            cross_product = (x0 - x1)*(y2 - y1) - (y0 - y1)*(x2 - x1); //外積のZ軸計算
                            norm1 = hypot(x1 - x0,y1 - y0);//ノルム1
                            norm2 = hypot(x1 - x2,y1 - y2);//ノルム2

                            rad = acos(dot_product/(norm1*norm2)); //3点の角度計算
                            deg = rad * 180 / M_PI;
                            deg = 180 - deg;
                            if(deg > 100){
                                steps = (int)(deg/5);
                            }
                            else if(deg > 50){
                                steps = (int)(deg/4);
                            }
                            else if(deg > 3){
                                steps = (int)(deg/3);
                            }
                            else if(deg > 0.5){
                                steps = (int)(deg/0.5);
                            }
                            else{
                                steps = 1;
                            }
                        
                            rotation_step = deg / steps;

                            //時計回りと反時計回り
                            if(cross_product >= 0.0){
                                rotation = rotation + rotation_step;
                            }
                            else if(cross_product < 0.0){
                                rotation = rotation - rotation_step;
                            }
                        
                            vehicle_steprotation++;
                            //回転が終了したら
                            if(vehicle_steprotation >= steps){
                                vehicle_steprotation = 0;
                                mode = 1; //次は移動
                            }
                        }  
                        //交差点を表示
                        if(word_mode == 0){
                            draw_intersection_name(vehicle_pathIterator,path,-rotation - rotation_x, rotation_z);
                        }
                        if(word_mode == 1){
                            draw_intersection_pathname(path,-rotation - rotation_x, rotation_z);
                        }
                        if(word_mode == 2){
                            draw_intersection_allname(crossing_number,-rotation - rotation_x, rotation_z);
                        }

                        /* 移動体を表示 */
                        glColor3d(1.0, 1.0, 1.0);
                        draw_ball(ORIGIN_X, ORIGIN_Y, MARKER_RADIUS);
                        break;

                    case 1:
                        //移動体を進めて座標を計算する
                        if(path[vehicle_pathIterator + 0] != -1 &&path[vehicle_pathIterator + 1] != -1){
                            x0 = cross[path[vehicle_pathIterator + 0]].pos.x;
                            y0 = cross[path[vehicle_pathIterator + 0]].pos.y;
                            x1 = cross[path[vehicle_pathIterator + 1]].pos.x;
                            y1 = cross[path[vehicle_pathIterator + 1]].pos.y;
                            distance = hypot(x1 - x0, y1 - y0);

                            //distanceの大きさによってstepsの数を変える
                            if (distance >= 0.05){
                                steps = (int)(distance / 0.1);
                            }
                            else{
                                steps = (int)(distance / 0.01);
                            }
                            //ステップを増やして地図の動きを決める
                            vehicle_stepOnEdge++;
                            map_x = (x1 - x0) / steps;
                            map_y = (y1 - y0) / steps;

                            //交差点を表示
                            if(word_mode == 0){
                                draw_intersection_name(vehicle_pathIterator,path,-rotation - rotation_x, rotation_z);
                            }
                            if(word_mode == 1){
                                draw_intersection_pathname(path,-rotation - rotation_x, rotation_z);
                            }
                            if(word_mode == 2){
                                draw_intersection_allname(crossing_number,-rotation - rotation_x, rotation_z);
                            }

                            /* 移動体を表示 */
                            glColor3d(1.0, 1.0, 1.0);
                            draw_ball(ORIGIN_X, ORIGIN_Y, MARKER_RADIUS);

                            ORIGIN_X = ORIGIN_X + map_x;
                            ORIGIN_Y = ORIGIN_Y + map_y;

            
                            if(vehicle_stepOnEdge >= steps){
                                //交差点についたので回転後、次の道路へ
                                vehicle_pathIterator++;
                                vehicle_stepOnEdge = 0;
                                mode = 0;
                                
                            }
                        }
                        break;
                    
                    case 2:
                        //移動体を進めて座標を計算する
                        if(path[vehicle_pathIterator + 0] != -1 &&path[vehicle_pathIterator + 1] != -1){
                            x0 = cross[path[vehicle_pathIterator + 0]].pos.x;
                            y0 = cross[path[vehicle_pathIterator + 0]].pos.y;
                            x1 = cross[path[vehicle_pathIterator + 1]].pos.x;
                            y1 = cross[path[vehicle_pathIterator + 1]].pos.y;
                            distance = hypot(x1 - x0, y1 - y0);

                            //distanceの大きさによってstepsの数を変える
                            if (distance >= 0.05){
                                steps = (int)(distance / 0.1);
                            }
                            else{
                                steps = (int)(distance / 0.01);
                            }
                            //ステップを増やして地図の動きを決める
                            vehicle_stepOnEdge++;
                            map_x = (x1 - x0) / steps;
                            map_y = (y1 - y0) / steps;

                            //交差点を表示
                            if(word_mode == 0){
                                draw_intersection_name(vehicle_pathIterator,path,-rotation - rotation_x, rotation_z);
                            }
                            if(word_mode == 1){
                                draw_intersection_pathname(path,-rotation - rotation_x, rotation_z);
                            }
                            if(word_mode == 2){
                                draw_intersection_allname(crossing_number,-rotation - rotation_x, rotation_z);
                            }
                        
                            /* 移動体を表示 */
                            glColor3d(1.0, 1.0, 1.0);
                            draw_ball(ORIGIN_X, ORIGIN_Y, MARKER_RADIUS);

                            ORIGIN_X = ORIGIN_X + map_x;
                            ORIGIN_Y = ORIGIN_Y + map_y;

                            if(vehicle_stepOnEdge >= steps){
                                //交差点についたので回転後、次の道路へ
                                vehicle_pathIterator++;
                                vehicle_stepOnEdge = 0;
                            }
                        }
                        else{
                            //交差点を表示
                            if(word_mode == 0){
                                draw_intersection_name(vehicle_pathIterator,path,-rotation - rotation_x, rotation_z);
                            }
                            if(word_mode == 1){
                                draw_intersection_pathname(path,-rotation - rotation_x, rotation_z);
                            }
                            if(word_mode == 2){
                                draw_intersection_allname(crossing_number,-rotation - rotation_x, rotation_z);
                            }
                        
                            /* 移動体を表示 */
                            glColor3d(1.0, 1.0, 1.0);
                            draw_ball(ORIGIN_X, ORIGIN_Y, MARKER_RADIUS);
                        }
                        break;
                    
                    case 3:
                        //交差点と移動体のみを表示
                        //交差点を表示
                        if(word_mode == 0){
                            draw_intersection_name(vehicle_pathIterator,path,-rotation - rotation_x, rotation_z);
                        }
                        if(word_mode == 1){
                            draw_intersection_pathname(path,-rotation - rotation_x, rotation_z);
                        }
                        if(word_mode == 2){
                            draw_intersection_allname(crossing_number,-rotation - rotation_x, rotation_z);
                        }
                    
                        /* 移動体を表示 */
                        glColor3d(1.0, 1.0, 1.0);
                        draw_ball(ORIGIN_X, ORIGIN_Y, MARKER_RADIUS);

                        break;
                }

                glfwSwapBuffers();  /* フロントバッファとバックバッファを入れ替える */
                
                //もしループ抜ける準備ができたら少し待ってループを抜ける
                if(mode2 == 1){
                    wait_time = (int) 1000 / window_speed;
                    if(j >= wait_time){
                        j = 0;
                        mode2 = 0;
                        break;
                    }
                   j++;
                }

                //もし移動体が目的地に到着しそうなら、このループから抜ける準備をする
                if(path[vehicle_pathIterator + 0] == -1 || path[vehicle_pathIterator + 1] == -1){
                    mode2 = 1;
                }

                usleep(window_speed*1000);  //少しの時間
            }
            //ここに来たということは、目的地の到着している

        }
        
        loopend:

        glfwTerminate();
        printf("もう一度行いますか？\n");
        printf("1.もう一度行う Another number.カーナビを終了\n");
        printf("input>");
        scanf("%d",&choice);
        if(choice != 1){
            break;
        }
    }
    
    printf("\nカーナビ終了\n\n");

    return 0;
}

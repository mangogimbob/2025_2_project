#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

int all_dmg = 0; //전체 피해량

typedef struct {    // 각 객체 구조체
    char name[32];  //객체 이름
    int hp;             //피통
    int maxHp;      //최대체력
    int atk;            //공격력
    int def;            //방어력
    int spd;            //속도
    double critRate;    // 치명타 확률, 0.0 ~ 1.0 사이의 값으로 
    double critMultiplier; // 치명타 피해, 0.5 => 50% 이고 치명타 발생시 1.5배 피해 
    int isDefending;    // 현재 방어중인가? (플레이어용)
    int defenseCooldown;    // 전 턴에 방어를 사용하였는가?  1이면 다음 턴 사용 불가 (플레이어용)

    int isCharging;     // 1이면 차징중 (몹용)
    int patternCount;   // 보스나 몹의 패턴 순서 관리용
} Character;

typedef struct {    // 축복 구조체
    char id[32];        //축복 ID
    char ext[128];  //축복 설명
    char grade[16];   //축복 등급: 일반, 희귀, 전설, ???

    double hpPercent;   // 피통 증가량,  0.2가 증가하면  maxHp * 1.2 로 적용
    double atkPercent;  // 공격력 증가량, 0.25가 증가하면  atk * 1.25 로 적용
    int spdAdd;         // 속도 증가량, 10 만큼 증가하면 spd + 10 로 적용
    double critRateAdd;     // 치명타 확률 증가, 0.05 는 5% 증가 
    double critMultiplierAdd; // 치명타 피해 증가, 0.2 는 20% 증가
} Blessing;

typedef struct {        // 아이템 구조체
    char id[32];          // 아이템 ID
    char name[64];      // 아이템 이름
    char desc[128];     // 아이템 설명
    int consumable; // 사용가능하며 소모되는 아이템이면 1, 패시브 아이템이면 0
    int duration;   // 효과 지속 턴수이며 n의 값을 가지면 n턴만큼 지속, 0이면 즉발, -1이면 전투 내에서 영구적으로 지속 
} Item;

typedef struct {    // 플레이어 인벤토리 구조체
    Item items[10];
    int count;      // 현재 아이템 개수
} Inventory;

typedef struct { //지속 효과 관리
    char id[32];
    int remaining; // 남은 턴
} ActiveEffect;

Inventory playerInventory; //인벤토리
ActiveEffect activeEffects[16]; //지속효과
int activeEffectCount = 0;

int passive_ddak_applied = 0; // 딱정벌레 펜던트 적용 여부
int passive_redpend_applied = 0; //붉은 펜던트 적용 여부
int passive_contract_applied = 0; //계약서(영구 아님, 지속효과로 적용 시 1),(사용시 activeEffect로 처리)

void useItem_bandage(Character* player);
int coinFlip();
void applyBlessingSimple(Character* player, const Blessing* b, const Item allItems[]);
int calcDamage(Character* attacker, Character* defender, int* outIsCrit, Character* player);
void doAttack(Character* src, Character* dst, Character* player);


void textcolor(int colorNum) {    //CMD 글자 색 변경 함수
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), colorNum);
}

void pauseLine() {      //플레이어가 텍스트를 모두 읽을 수 있도록 일시정지하는 함수
    printf("계속하려면 Enter를 누르세요...");
    fflush(stdout);
    getchar(); // 사용자 입력 대기
}
void clearConsole() { system("cls"); }

void drawBattleUI(const Character* player, const Character* enemy, double pGauge, double eGauge, int playerTurn) {    //기본적인 전투UI, 플레이어와 적의 현재 상태를 보여준다
    clearConsole();
    textcolor(15);
    printf("-------------------------------------------------------------------------------------\n");
    printf("|   [행동 순서] >>");

    // 시뮬레이션용 임시 변수 생성
    double simP = pGauge;
    double simE = eGauge;
    double pSpd = (player->spd > 0) ? player->spd : 1.0;
    double eSpd = (enemy->spd > 0) ? enemy->spd : 1.0;

	for (int i = 0; i < 5; i++) {       //현재를 기점으로 앞으로의 5턴의 행동 순서 계산
		double neededP = (simP >= 100.0) ? 0.0 : (100.0 - simP);    //플레이어의 행동게이지가 100이 될 때까지 필요한 양
		double neededE = (simE >= 100.0) ? 0.0 : (100.0 - simE);    //적의 행동게이지가 100이 될 때까지 필요한 양
		double ticksP = neededP / pSpd; //플레이어의 행동게이지가 100이 되기까지 필요한 틱 수
		double ticksE = neededE / eSpd; //적의 행동게이지가 100이 되기까지 필요한 틱 수

        if (ticksP <= ticksE) { // 플레이어가 빠르면
            textcolor(11); 
            printf("[%s]", player->name); 
            simP += ticksP * pSpd - 100.0; //플레이어 행동 게이지 소모
			simE += ticksP * eSpd;         // 적 행동 게이지 증가
        }
        else { // 적이 빠르면
            textcolor(12); 
            printf("[%s]", enemy->name);
			simP += ticksE * pSpd;  // 플레이어 행동 게이지 증가
			simE += ticksE * eSpd - 100.0;  //적  행동 게이지 소모
        }
        textcolor(8); if (i < 4) printf(" -> "); 
    }
    textcolor(15);
    printf("\n");
    printf("|\n");
    printf("|                                                          [%s]\n", enemy->name);

    int ebar = (int)((double)enemy->hp / enemy->maxHp * 20.0 + 0.0001); // 20칸 기준
    if (ebar < 0) ebar = 0; if (ebar > 20) ebar = 20;
    printf("|                                                    HP: [");
    textcolor(4);//적 HP바는 빨간색
    for (int i = 0; i < ebar; i++) putchar('#');
    for (int i = ebar; i < 20; i++) putchar('-');
    textcolor(15);
    printf("] %d/%d |\n", enemy->hp, enemy->maxHp);

    printf("|\n"); printf("|\n"); printf("|\n"); printf("|\n"); printf("|\n");

    // 플레이어 HP바
    int pbar = (int)((double)player->hp / player->maxHp * 20.0 + 0.0001);
    if (pbar < 0) pbar = 0; if (pbar > 20) pbar = 20;
    printf("| %s                                                            \n", player->name);
    printf("| HP: [");
    textcolor(9);//플레이어 HP바는 파란색
    for (int i = 0; i < pbar; i++) putchar('#');
    for (int i = pbar; i < 20; i++) putchar('-');
    textcolor(15);
    printf("]  (%d/%d)                                                  \n", player->hp, player->maxHp);
    printf("| ");
	if (activeEffectCount > 0) {        //지속효과가 있을 경우 효과 표시
        textcolor(14); // Yellow
        printf("효과: ");
        for (int i = 0; i < activeEffectCount; i++) {
            // crit_buff는 플레이어에게만 보이도록 이름을 변경
            if (strcmp(activeEffects[i].id, "crit_buff") == 0) {
                printf("치명타 확룰 증가(%d턴) ", activeEffects[i].remaining);
            }
            else {
                printf("%s(%d턴) ", activeEffects[i].id, activeEffects[i].remaining);
            }
        }
        textcolor(15);
    }
    printf("\n"); // 줄바꿈

    printf("-------------------------------------------------------------------------------------\n");
    if (playerTurn)
        printf("| %s의 턴 :                                                                  \n", player->name);
    else
        printf("| 적의 턴 :                                                                          \n");
    printf("| ");
    textcolor(12);
    printf("[1] 공격  ");
    textcolor(9);
    printf("[2] 방어  ");
    textcolor(10);
    printf("[3] 아이템\n");
    textcolor(15);
    printf("------------------------------------------------------------------------------------\n");
}

int calcDamage(Character* attacker, Character* defender, int* outIsCrit, Character* player) {       //데미지 계산

    if (strstr(defender->name, "모기") != NULL && attacker == player) {
        if (rand() % 100 < 30) {        //모기는 30% 확률로 플레이어의 공격을 회피
            textcolor(11); // 밝은 청록
            printf(">> %s(이)가 공격을 피했습니다!\n", defender->name);
            textcolor(15);
            return 0; // 데미지 0 리턴
        }
    }

    int base = attacker->atk - defender->def / 2;       //기본 데미지 계산 수식, 방어력이 데미지에 절반만큼 영향을 미침
    if (base < 1) base = 1;

    double currentCritRate = attacker->critRate;
    if (attacker == player && getEffectRemaining("crit_buff") > 0) {
        currentCritRate += 0.5; // 치명타 확률 50% 증가
    }

    double roll = (double)rand() / RAND_MAX;
    int isCrit = (roll < currentCritRate) ? 1 : 0;      // currentCritRate 사용
    *outIsCrit = isCrit;

    double dmgd = (double)base;
    if (isCrit) dmgd *= attacker->critMultiplier;       //치명타가 발생했을 경우 데미지에 치명타 배율을 곱한다.

    int jitter = rand() % 3 - 1; //데미지에 약간의 변동을 주기위한 난수 생성, -1,0,1 중 하나가 나온다
    dmgd += jitter;

    if (dmgd < 1.0) {       //최소 데미지는 1로 고정
        dmgd = 1.0;
    }

    int damage = (int)(dmgd + 0.0001);       //소수점 버리기

    if (attacker->isCharging) {     //차징 공격일 때 데미지 2.5배 증가
        damage = (int)(damage * 2.5);
        attacker->isCharging = 0; // 기 모으기 해제
    }

    if (defender->isDefending) {            //방어중일 때 받는 데미지 90% 감소효과
        damage = (int)(damage * 0.1 + 0.0001);
        if (damage < 1) damage = 1;
    }

    if (getEffectRemaining("contract") > 0 && defender == player) { // 플레이어가 맞을 때
        damage = damage * 2;
    }

    return damage;
}

int findItemIndexInInventoryById(const char* id) {                          //아이템 ID로 인벤토리 내 아이템 인덱스 찾기
    for (int i = 0; i < playerInventory.count; i++) {
        if (strcmp(playerInventory.items[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

char acquiredBlessings[32][32];     //획득한 축복 ID 목록
int acquiredBlessingCount = 0;      //획득한 축복 개수

int hasBlessingById(const char* id) {
    for (int i = 0; i < acquiredBlessingCount; i++) {
        if (strcmp(acquiredBlessings[i], id) == 0) return 1;
    }
    return 0;
}


int hasItemInInventory(const char* id) {    //인벤토리에 해당 아이템이 있는지 확인
    return findItemIndexInInventoryById(id) >= 0;
}

void removeItemFromInventoryByIndex(int idx) { //사용한 아이템 인벤토리에서 제거
    if (idx < 0 || idx >= playerInventory.count) {
        return;
    }
    for (int i = idx; i < playerInventory.count - 1; i++) {
        playerInventory.items[i] = playerInventory.items[i + 1];
    }
    playerInventory.count--;
}

void addItemToInventory(const Item* it) { //인벤토리에 아이템 추가
    if (playerInventory.count >= 10) {
        printf("인벤토리가 가득 찼습니다!\n");
        return;
    }
    playerInventory.items[playerInventory.count++] = *it;
}

void startEffect(const char* id, int duration) {// 효과 활성화 
    if (activeEffectCount >= 16)
        return;      // 이미 효과가 있는지 확인 (중복 방지)
    for (int i = 0; i < activeEffectCount; i++) {
        if (strcmp(activeEffects[i].id, id) == 0) {
            activeEffects[i].remaining = duration; // 지속시간 갱신
            return;
        }
    }
    strcpy(activeEffects[activeEffectCount].id, id);
    activeEffects[activeEffectCount].remaining = duration;
    activeEffectCount++;
}

void endEffectByIndex(int idx) { // 효과 종료
    if (idx < 0 || idx >= activeEffectCount) return;
    for (int i = idx; i < activeEffectCount - 1; i++) {
        activeEffects[i] = activeEffects[i + 1];
    }
    activeEffectCount--;
}

int getEffectRemaining(const char* id) { // 효과 남은 턴수 반환
    for (int i = 0; i < activeEffectCount; i++) {
        if (strcmp(activeEffects[i].id, id) == 0) return activeEffects[i].remaining;
    }
    return 0;
}

int adrenaline_spd_change = 0;

void tickActiveEffects(Character* player) {//매 턴마다 활성효과 관리( 지속시간 감소 및 종료 )
    for (int i = 0; i < activeEffectCount; ) {
        activeEffects[i].remaining--;
        if (activeEffects[i].remaining <= 0) {        // 효과 종료
            if (strcmp(activeEffects[i].id, "adrenaline") == 0) {

                player->spd -= adrenaline_spd_change;
                adrenaline_spd_change = 0; // 초기화
                printf("아드레날린 효과가 종료되어 속도가 원래대로 돌아옵니다.\n");
            }
            if (strcmp(activeEffects[i].id, "contract") == 0) {
                printf("찢겨진 계약서 효과가 종료되었습니다.\n");
            }
            if (strcmp(activeEffects[i].id, "energy") == 0) {
                printf("에너지 드링크 효과가 종료되었습니다.\n");
            }
            endEffectByIndex(i);      //인덱스 i의 효과 종료
        }
        else {
            i++;
        }
    }
}

int ddak_spd_change = 0; // 딱정벌레 펜던트로 인한 속도 변화량 저장용
int ddak_def_change = 0; // 딱정벌레 펜던트로 인한 방어력 변화량 저장용

void applyPassiveItemsToPlayer_fixed(Character* player) { //패시브 아이템 적용
    ddak_spd_change = 0; ddak_def_change = 0;
    passive_ddak_applied = 0;
    passive_redpend_applied = 0;
    if (hasItemInInventory("ddakpend")) {
        ddak_spd_change = (int)(player->spd * 0.30 + 0.0001);
        ddak_def_change = (int)(player->def * 0.30 + 0.0001);
        player->spd -= ddak_spd_change;
        player->def += ddak_def_change;
        passive_ddak_applied = 1;
        textcolor(9);
        printf("딱정벌레 펜던트");
        textcolor(15);
        printf(" 효과: 속도 %d 감소, 방어 %d 증가.\n", ddak_spd_change, ddak_def_change);
    }
    if (hasItemInInventory("redpend")) {
        passive_redpend_applied = 1;
        textcolor(12);
        printf("붉은 눈물 펜던트");
        textcolor(15);
        printf(" 효과: 공격 시 30%% 흡혈.\n");
    }
}

void revertPassiveItemsFromPlayer_fixed(Character* player) { // new
    if (passive_ddak_applied) {
        player->spd += ddak_spd_change;
        player->def -= ddak_def_change;
        printf("딱정벌레 펜던트 효과를 원복합니다.\n");
    }
    passive_ddak_applied = 0;
    passive_redpend_applied = 0;
    ddak_spd_change = ddak_def_change = 0;
}

int forcedHeadsRemaining = 0; //남은 동전 앞면 횟수

void showInventoryMenu(Character* player) { // new
    while (1) {
        printf("---- 인벤토리 ----\n");
        if (playerInventory.count == 0) {
            printf("(비어있음)\n");
            printf("계속하려면 Enter\n");
            getchar();
            return;
        }
        for (int i = 0; i < playerInventory.count; i++) {
            printf("[%d] %s \n   설명: %s\n", i + 1, playerInventory.items[i].name, playerInventory.items[i].desc);
        }
        printf("[0] 취소\n");
        printf("사용할 아이템 번호 입력: ");
        char buf[32];
        if (!fgets(buf, sizeof(buf), stdin)) return;
        int sel = 0;
        if (sscanf(buf, "%d", &sel) != 1) continue;
        if (sel == 0) return;
        if (sel < 1 || sel > playerInventory.count) continue;
        int idx = sel - 1;
        Item chosen = playerInventory.items[idx];
        if (strcmp(chosen.id, "bandage") == 0) { // 붕대 : 즉시 회복
            useItem_bandage(player);
            if (chosen.consumable)
                removeItemFromInventoryByIndex(idx);
            pauseLine();
            return;
        }
        else if (strcmp(chosen.id, "energy") == 0) { // 에너지 드링크
            startEffect("energy", chosen.duration); //지속턴 수 : 3
            if (chosen.consumable)
                removeItemFromInventoryByIndex(idx);
            printf("에너지 드링크를 사용하여 3턴 동안 턴 시작시 회복 효과를 얻었습니다.\n");
            pauseLine();
            return;
        }
        else if (strcmp(chosen.id, "adrenaline") == 0) { // 아드레날린

            if (getEffectRemaining("adrenaline") > 0) {
                printf("이미 아드레날린 효과가 적용 중입니다.\n");
                pauseLine();
                continue;
            }
            int spd_increase = (int)(player->spd * 0.5 + 0.0001);
            player->spd += spd_increase;
            startEffect("adrenaline", chosen.duration);

            adrenaline_spd_change = spd_increase;

            if (chosen.consumable)
                removeItemFromInventoryByIndex(idx);
            printf("아드레날린 주사를 사용하여 %d 만큼 속도가 증가했습니다 (2턴).\n", spd_increase);
            pauseLine();
            return;
        }
        else if (strcmp(chosen.id, "lucky") == 0) { // 네잎클로버
            startEffect("lucky", chosen.duration); // (지속시간은 3이지만, 여기선 횟수로)
            forcedHeadsRemaining = 3;

            if (chosen.consumable)
                removeItemFromInventoryByIndex(idx);
            printf("네잎클로버를 사용했습니다. 다음 3번의 동전 던지기에서 모두 앞면이 됩니다.\n");
            pauseLine();
            return;
        }
        else if (strcmp(chosen.id, "chip") == 0) { // 파산한 도박사의 칩
            int coin = coinFlip();
            if (coin == 1) { //승리 (앞면)
                printf("도박사의 칩: 앞면! 전투에서 즉시 승리합니다.\n");
                startEffect("chip_win", 1);
                if (chosen.consumable)
                    removeItemFromInventoryByIndex(idx);
                pauseLine();
                return;
            }
            else {
                printf("도박사의 칩: 뒷면... 아이템을 모두 잃었습니다.\n");
                if (chosen.consumable)
                    removeItemFromInventoryByIndex(idx); // 칩은 먼저 제거
                playerInventory.count = 0; // 모든 아이템 제거
                pauseLine();
                return;
            }
        }
        else if (strcmp(chosen.id, "contract") == 0) { // 찢겨진 계약서
            startEffect("contract", chosen.duration);
            if (chosen.consumable)
                removeItemFromInventoryByIndex(idx);
            printf("찢겨진 계약서를 사용했습니다. 10턴 동안 피해 증가/수신 증가 효과 발동.\n");
            pauseLine();
            return;
        }
        else { // 미구현
            if (!chosen.consumable) {
                printf("%s(은)는 장착형 아이템입니다. 이미 인벤토리에 존재합니다.\n", chosen.name);
                pauseLine();
                continue;
            }
            else {
                printf("이 아이템의 사용 효과는 아직 구현되지 않았습니다.\n");
                pauseLine();
                continue;
            }
        }
    }
}


void doAttack(Character* src, Character* dst, Character* player) {

    int isCrit = 0;
    int dmg = calcDamage(src, dst, &isCrit, player);

    if (getEffectRemaining("contract") > 0 && src == player) {       // 찢겨진 계약서 효과: 가하는 피해 2배
        dmg = dmg * 2;
    }

    if (hasBlessingById("rare_coinAtk") && src == player) {
        int coin = coinFlip();
        printf("동전을 던집니다");
        Sleep(200);
        printf(".");
        Sleep(200);
        printf(".");
        Sleep(200);
        printf(".\n");
        Sleep(100);
        if (coin == 1) { // 앞면
            textcolor(14);
            printf("동전 앞면! 피해량 2배!\n");
            textcolor(15);
            dmg = dmg * 2;
        }
        else { // 뒷면
            textcolor(8);
            printf("동전 뒷면... 피해량 절반.\n");
            textcolor(15);
            dmg = dmg / 2;
            if (dmg < 1) dmg = 1; // 최소 데미지 1
        }
    }

    all_dmg += dmg; //전체 피해량 누적
    dst->hp -= dmg;       // 데미지 적용
    if (dst->hp < 0)
        dst->hp = 0;       // 체력 음수 방지

    printf("%s의 공격! %d의 데미지", src->name, dmg);
	if (src == player) {    // 플레이어가 공격한 경우
        printf("를 가했다!");
	}
	else {                        // 적이 공격한 경우
        printf("를 받았다.");
    }
    if (isCrit) {
        printf("(");
        textcolor(12);
        printf("치명타!");
        textcolor(15);
        printf(")\n");
    }
    else {
        printf("\n");
    }

    if (src == player && hasBlessingById("gen_getCritRate")) {
        if (!isCrit) {
            // 이번 공격이 치명타가 아니었다면
            if (getEffectRemaining("crit_buff") == 0) { // 버프가 중첩되지 않도록
                printf("축복 효과(치명타 확률 증가): 다음 턴까지 치명타 확률 +50%%!\n");
                startEffect("crit_buff", 2); // 이번 턴 끝, 다음 턴 끝
            }
        }
    }

    if (passive_redpend_applied && src == player) {
        int heal = (int)(dmg * 0.30 + 0.0001);
        src->hp += heal;
        if (src->hp > src->maxHp) src->hp = src->maxHp;
        textcolor(10);
        printf("%s의 붉은 펜던트 효과로 체력 %d 회복!\n", src->name, heal);
        textcolor(15);
    }

}

void doDefend(Character* player) { //방어 액션 처리
    if (player->defenseCooldown > 0) {
        textcolor(12);
        printf("이번 턴에는 방어를 사용할 수 없습니다.\n", player->defenseCooldown - 1);
        textcolor(15);
        return;
    }
    player->isDefending = 1;
    player->defenseCooldown = 2; // 2턴 쿨다운
    textcolor(10);
    printf("%s가 방어 자세를 취했습니다.\n", player->name);
    textcolor(15);
}


int coinFlip() {
    if (forcedHeadsRemaining > 0) {
        forcedHeadsRemaining--;
        printf("네잎클로버 효과! (앞면)\n");
        return 1; //앞면
    }
    return rand() % 2; // 0 (뒷면) 또는 1 (앞면)
}

void initBlessings(Blessing arr[], int n) {
    for (int i = 0; i < n; i++) { // 배열 초기화 
        memset(&arr[i], 0, sizeof(Blessing));
    }

    //일반 등급
    strcpy(arr[0].id, "gen_maxhp20"); strcpy(arr[0].ext, "최대 체력 +20%"); strcpy(arr[0].grade, "일반");
    arr[0].hpPercent = 0.20; arr[0].atkPercent = 0.0; arr[0].spdAdd = 0; arr[0].critRateAdd = 0; arr[0].critMultiplierAdd = 0;

    strcpy(arr[1].id, "gen_atk25"); strcpy(arr[1].ext, "공격력 +25%"); strcpy(arr[1].grade, "일반");
    arr[1].hpPercent = 0.0; arr[1].atkPercent = 0.25; arr[1].spdAdd = 0; arr[1].critRateAdd = 0; arr[1].critMultiplierAdd = 0;

    strcpy(arr[2].id, "gen_spd10"); strcpy(arr[2].ext, "속도 +10"); strcpy(arr[2].grade, "일반");
    arr[2].hpPercent = 0.0; arr[2].atkPercent = 0.0; arr[2].spdAdd = 10; arr[2].critRateAdd = 0; arr[2].critMultiplierAdd = 0;

    strcpy(arr[3].id, "gen_crit5"); strcpy(arr[3].ext, "치명타 확률 +5%"); strcpy(arr[3].grade, "일반");
    arr[3].hpPercent = 0.0; arr[3].atkPercent = 0.0; arr[3].spdAdd = 0; arr[3].critRateAdd = 0.05; arr[3].critMultiplierAdd = 0;

    strcpy(arr[4].id, "gen_crit20"); strcpy(arr[4].ext, "치명타 피해 +20%"); strcpy(arr[4].grade, "일반");
    arr[4].hpPercent = 0.0; arr[4].atkPercent = 0.0; arr[4].spdAdd = 0; arr[4].critRateAdd = 0; arr[4].critMultiplierAdd = 0.2;

    strcpy(arr[5].id, "gen_getItem"); strcpy(arr[5].ext, "무작위 아이템 하나를 즉시 획득한다."); strcpy(arr[5].grade, "일반");
    arr[5].hpPercent = 0.0; arr[5].atkPercent = 0.0; arr[5].spdAdd = 0; arr[5].critRateAdd = 0; arr[5].critMultiplierAdd = 0;

    strcpy(arr[6].id, "gen_getHP20"); strcpy(arr[6].ext, "전투 중 자신의 턴이 시작할 시, 체력을 10 회복한다."); strcpy(arr[6].grade, "일반");
    arr[6].hpPercent = 0.0; arr[6].atkPercent = 0.0; arr[6].spdAdd = 0; arr[6].critRateAdd = 0; arr[6].critMultiplierAdd = 0;

    strcpy(arr[7].id, "gen_halfHP_atkUP"); strcpy(arr[7].ext, "자신의 최대 체력이 절반으로 감소하고, 공격력이 2.5배가 된다."); strcpy(arr[7].grade, "일반");
    arr[7].hpPercent = -0.5; arr[7].atkPercent = 2.5; arr[7].spdAdd = 0; arr[7].critRateAdd = 0; arr[7].critMultiplierAdd = 0;

    strcpy(arr[8].id, "gen_getCritRate"); strcpy(arr[8].ext, "공격이 치명타가 아닐 경우, 다음 턴까지 치명타 확률이 50% 증가한다."); strcpy(arr[8].grade, "일반");
    arr[8].hpPercent = 0.0; arr[8].atkPercent = 0.0; arr[8].spdAdd = 0; arr[8].critRateAdd = 0; arr[8].critMultiplierAdd = 0;


    //희귀 등급
    strcpy(arr[9].id, "rare_critrt15"); strcpy(arr[9].ext, "치명타 확률 +15%"); strcpy(arr[9].grade, "희귀");
    arr[9].hpPercent = 0; arr[9].atkPercent = 0; arr[9].spdAdd = 0; arr[9].critRateAdd = 0.15; arr[9].critMultiplierAdd = 0;

    strcpy(arr[10].id, "rare_atk30"); strcpy(arr[10].ext, "공격력 +30%"); strcpy(arr[10].grade, "희귀");
    arr[10].hpPercent = 0; arr[10].atkPercent = 0.30; arr[10].spdAdd = 0; arr[10].critRateAdd = 0; arr[10].critMultiplierAdd = 0;

    strcpy(arr[11].id, "rare_coinAtk"); strcpy(arr[11].ext, "매번 공격할때마다 동전을 던진다, 앞면이 나오면 피해량 2배, 뒷면이면 절반."); strcpy(arr[11].grade, "희귀");
    arr[11].hpPercent = 0; arr[11].atkPercent = 0.0; arr[11].spdAdd = 0; arr[11].critRateAdd = 0; arr[11].critMultiplierAdd = 0;

    //전설 등급
    strcpy(arr[12].id, "leg_crit45"); strcpy(arr[12].ext, "치명타 확률 +45%"); strcpy(arr[12].grade, "전설");
    arr[12].hpPercent = 0; arr[12].atkPercent = 0; arr[12].spdAdd = 0; arr[12].critRateAdd = 0.45; arr[12].critMultiplierAdd = 0;

    //익시드 등급
    strcpy(arr[13].id, "exc_critrt100"); strcpy(arr[13].ext, "치명타 확률 +100%"); strcpy(arr[13].grade, "???");
    arr[13].hpPercent = 0; arr[13].atkPercent = 0; arr[13].spdAdd = 0; arr[13].critRateAdd = 1.0; arr[13].critMultiplierAdd = 0;
}

void applyBlessingSimple(Character* player, const Blessing* b, const Item allItems[]) {       //축복 적용
    strcpy(acquiredBlessings[acquiredBlessingCount], b->id);
    acquiredBlessingCount++;

    if (strcmp(b->id, "gen_getItem") == 0) {
        int itemIdx = rand() % 8; // 8은 initItems에 정의된 아이템 총 개수
        addItemToInventory(&allItems[itemIdx]);
        printf("축복 효과로 [%s] 획득!\n", allItems[itemIdx].name);
    }

    if (strcmp(b->id, "gen_halfHP_atkUP") == 0) {
        // atkPercent = 2.5 
        player->atk = (int)(player->atk * b->atkPercent);
    }

    if (b->hpPercent != 0.0) {       // 최대 체력 증가/감소
        int oldMaxHp = player->maxHp;
        player->maxHp += (int)(oldMaxHp * b->hpPercent);
        if (player->maxHp < 1) player->maxHp = 1; // 체력 0 방지
        player->hp = player->maxHp; // 체력 완전 회복
    }

    if (b->atkPercent != 0.0 && strcmp(b->id, "gen_halfHP_atkUP") != 0) {
        player->atk += (int)(player->atk * b->atkPercent);
    }

    if (b->spdAdd != 0) player->spd += b->spdAdd;   // 속도 증가
    if (b->critRateAdd != 0) player->critRate += b->critRateAdd;  // 치명타 확률 증가
    if (b->critMultiplierAdd != 0) player->critMultiplier += b->critMultiplierAdd;  // 치명타 피해 증가
}


void initItems(Item items[], int n) {       //아이템 초기화
    strcpy(items[0].id, "bandage");
    strcpy(items[0].name, "붕대");
    strcpy(items[0].desc, "최대 체력의 20%를 회복한다.");
    items[0].consumable = 1; items[0].duration = 0;

    strcpy(items[1].id, "energy");
    strcpy(items[1].name, "에너지 드링크");
    strcpy(items[1].desc, "3턴 동안 턴 시작시 최대HP의 30%를 회복한다");
    items[1].consumable = 1; items[1].duration = 3;

    strcpy(items[2].id, "adrenaline");
    strcpy(items[2].name, "아드레날린 주사");
    strcpy(items[2].desc, "2턴 동안 속도가 50% 증가한다");
    items[2].consumable = 1; items[2].duration = 2;

    strcpy(items[3].id, "lucky");
    strcpy(items[3].name, "네잎클로버");
    strcpy(items[3].desc, "앞으로 3번의 동전 던지기에서 모두 앞면이 나온다");
    items[3].consumable = 1; items[3].duration = 3; // 횟수 기반이지만 턴으로 일단 표시

    strcpy(items[4].id, "redpend");
    strcpy(items[4].name, "붉은 눈물 펜던트");
    strcpy(items[4].desc, "소유시 몹에게 가하는 피해의 30% 만큼 체력을 회복한다.");
    items[4].consumable = 0; items[4].duration = -1;

    strcpy(items[5].id, "ddakpend");
    strcpy(items[5].name, "딱정벌레 펜던트");
    strcpy(items[5].desc, "소유시 속도가 30% 감소하고, 방어력이 30% 증가하며, 피격 후 자신의 최대 HP의 15%만큼의 HP를 회복한다.");
    items[5].consumable = 0; items[5].duration = -1;

    strcpy(items[6].id, "chip");
    strcpy(items[6].name, "파산한 도박사의 칩");
    strcpy(items[6].desc, "동전 던지기를 하여 앞면이 나오면 전투에서 즉시 승리하고, 뒷면이 나오면 자신의 아이템을 전부 잃는다.");
    items[6].consumable = 1; items[6].duration = 0;

    strcpy(items[7].id, "contract");
    strcpy(items[7].name, "찢겨진 계약서");
    strcpy(items[7].desc, "10턴동안 가하는 피해가 두 배가 되지만, 받는 피해 역시 두배가 된다.");
    items[7].consumable = 1; items[7].duration = 10;

}

void useItem_bandage(Character* player) {       //붕대 아이템 사용
    int heal = (int)(player->maxHp * 0.20);
    player->hp += heal;
    if (player->hp > player->maxHp) player->hp = player->maxHp;
    printf("붕대를 사용하여 HP를 %d 회복했습니다.\n", heal);
}

void processEnemyAction(Character* enemy, Character* player) {      //적 행동 처리
    printf("\n");

    if (strcmp(enemy->name, "보스") == 0) {       // 보스 패턴

        if (enemy->hp <= enemy->maxHp / 2 && enemy->patternCount == 0) {// 체력이 50% 이하면 광폭화
            enemy->patternCount = 1; // 광폭화 완료
            enemy->atk += 10;       //공증
            enemy->spd += 5;        //속증
            textcolor(12); // 빨강
            printf("        HP가 감소하여 %s(이)가 격분합니다!\n", enemy->name);
            textcolor(15);
            Sleep(1000);
            return;
        }

        if (enemy->isCharging == 0) {       //필살기
            int chance = (enemy->patternCount == 1) ? 30 : 15; // 광폭화시 30%, 평소 15%
            if (rand() % 100 < chance) {
                enemy->isCharging = 1;          // 기 모으기
                textcolor(13); // 보라색
                printf("      %s(이)가 거대한 에너지를 모으기 시작합니다...\n", enemy->name);
                textcolor(15);
                return;
            }
        }
    }
    else if (strstr(enemy->name, "골렘") != NULL) {       //골렘
        if (enemy->isCharging == 0) {
            if (rand() % 100 < 25) {        //25퍼
                enemy->isCharging = 1;
                textcolor(14); // 노랑
                if (strstr(enemy->name, "바위") != NULL) {
                    printf("    바위 골렘이 육중한 팔을 들어 올립니다!!\n");
                }
                else if (strstr(enemy->name, "얼음") != NULL) {
                    printf("    얼음 골렘이 한기를 내뿜습니다!!\n");
                }
                textcolor(15);
                return;
            }
        }
    }
    else if (strstr(enemy->name, "기사") != NULL) {       //기사들
        if (enemy->isCharging == 0) {
            if (rand() % 100 < 25) {        //25퍼
                enemy->isCharging = 1;
                textcolor(14); // 노랑
                if (strstr(enemy->name, "대검") != NULL) {
                    printf("    %s가 대검을 뒤로 빼며 자세를 잡습니다..\n", enemy->name);
                }
                else if (strstr(enemy->name, "랜스") != NULL) {
                    printf("    %s가 말 위에 올라타 자세를 잡습니다..\n", enemy->name);
                }
                else if (strstr(enemy->name, "석궁") != NULL) {
                    printf("    %s가 당신을 조준합니다..\n", enemy->name);
                }
                textcolor(15);
                return;
            }
        }
    }

    if (enemy->isCharging) {//차징공격
        textcolor(12);//빨강
        if (strstr(enemy->name, "골렘") != NULL) {
            printf("    %s의 강력한 일격!!\n", enemy->name);
        }
        else if (strstr(enemy->name, "대검") != NULL) {
            printf("    %s가 크게 검을 휘두릅니다!!\n", enemy->name);
        }
        else if (strstr(enemy->name, "랜스") != NULL) {
            printf("    %s가 당신을 향해 돌진합니다!!\n", enemy->name);
        }
        else if (strstr(enemy->name, "석궁") != NULL) {
            printf("    %s가 방아쇠를 당깁니다!!\n", enemy->name);
        }
        else {
            printf("    %s(이)가 모은 에너지를 폭발시킵니다!!\n", enemy->name);
        }
        textcolor(15);
    }

    doAttack(enemy, player, player);
}


int main(void) {
    srand((unsigned int)time(NULL));

    Blessing allBless[14];
    initBlessings(allBless, 14);
    Item allItems[8];
    initItems(allItems, 8);

    playerInventory.count = 0; // 인벤토리 초기화
    addItemToInventory(&allItems[0]); // bandage 붕대

    addItemToInventory(&allItems[1]); // energy 에너지드링크
    addItemToInventory(&allItems[4]); // redpend 붉은 펜던트
    addItemToInventory(&allItems[5]); // ddakpend 딱정벌레 펜던트
	addItemToInventory(&allItems[6]);   // chip 도박사의 칩

    Character player;
    printf("여행자여.. 당신의 이름은 무엇인가?\n      >>");
    fgets(player.name, sizeof(player.name), stdin);
    player.name[strcspn(player.name, "\n")] = '\0';

    if(strcspn(player.name, "\n") == 0) {
        strcpy(player.name, "여행자");
	}

    

    player.maxHp = player.hp = 50;       //기본스텟
    player.atk = 11;
    player.def = 6;
    player.spd = 10;
    player.critRate = 0.10;    // 기본 치확 10%
    player.critMultiplier = 1.5; // 기본 치피 150%
    player.isDefending = 0;
    player.defenseCooldown = 0;

    if (strcmp(player.name, "cheat") == 0) {
		player.maxHp = player.hp = 9999;
    }

    Character enemies[] = {         //이름,현재체력,최대체력,공격력,방어력,속도,치확,치피,방어?,방어쿨타임,0,0
    {"굶주린 개", 50, 50, 10, 4, 6, 0.05, 1.5, 0, 0,0,0},   
    {"바위 골렘",     70, 70, 15, 6, 5, 0.10, 1.5, 0, 0,0,0},
    {"엄청 커다란 모기",   30, 30, 8,  2, 10, 0.02, 1.3, 0, 0,0,0},
    {"11수 노인",    55, 55, 12, 5, 8, 0.07, 1.4, 0, 0,0,0},
    {"엄청 커다란 모기",   30, 30, 8,  2, 4, 0.02, 1.3, 0, 0,0,0},
    {"얼음 골렘",   50, 50, 10,  2, 4, 0.02, 2, 0, 0,0,0},
    {"대검을 든 호위기사",   80, 80, 12,  8, 6, 0.3, 1.5, 0, 0,0,0},
    {"랜스를 든 호위기사",   40, 40, 8, 12, 4, 0.02, 1.3, 0, 0,0,0},
    {"석궁을 든 호위기사",   40, 40, 8, 12, 4, 0.02, 1.3, 0, 0,0,0},
    };
    int numEnemies = sizeof(enemies) / sizeof(enemies[0]);      // 적 템플릿 수

    Character boss = { "보스", 300, 300, 18, 10, 7, 0.1, 1.5, 0, 0 ,0,0 };


    int pickedIndices[3]; //중복 방지용

    Blessing candidates[3];      // 축복 후보 3개 선택
    for (int i = 0; i < 3; i++) {
        int idx;
        int isDuplicate;
        do {
            idx = rand() % 14;   // 랜덤 선택
            isDuplicate = 0;     // 중복 여부 초기화

            for (int j = 0; j < i; j++) {
                if (pickedIndices[j] == idx) {
                    isDuplicate = 1; // 중복
                    break;
                }
            }

            if (isDuplicate == 0) { //중복 제거
                if (hasBlessingById(allBless[idx].id)) {
                    isDuplicate = 1;
                }
            }

        } while (isDuplicate);   // 중복일시 리트

        pickedIndices[i] = idx;          // 뽑은 숫자 기록
        candidates[i] = allBless[idx];   // 후보에 추가
    }
    printf("\n이중에서 하나 가져가게..\n");
    for (int i = 0; i < 3; i++) {
        printf("[%d] %s (", i + 1, candidates[i].ext);
        if (strcmp(candidates[i].grade, "일반") == 0)
            textcolor(15); // 흰색
        else if (strcmp(candidates[i].grade, "희귀") == 0)
            textcolor(9);  // 파란색
        else if (strcmp(candidates[i].grade, "전설") == 0)
            textcolor(14); // 노란색
        else
            textcolor(13);
        printf("%s", candidates[i].grade);
        textcolor(15);
        printf(")\n");
    }
    int sel = 1;
    char buf[32];
    fgets(buf, sizeof(buf), stdin);
    if (sscanf(buf, "%d", &sel) != 1 || sel < 1 || sel > 3) sel = 1;
    applyBlessingSimple(&player, &candidates[sel - 1], allItems);
    pauseLine();

    int battleCount = 0;
    while (battleCount < 7) {
        battleCount++;

        Character enemy;

        if (battleCount == 7) {
            clearConsole();
            printf("==============================\n");
            printf("어둠속에서 ");
            textcolor(12);
            printf("%s", boss.name);
            textcolor(15);
            printf("가 나타났습니다.\n");
            printf("==============================\n");
            enemy = boss;
            pauseLine();
        }
        else {
            enemy = enemies[rand() % numEnemies];   //잡몹 랜덤 설정
            enemy.maxHp += (battleCount - 1) * 5;
            enemy.hp = enemy.maxHp;
            enemy.atk += (battleCount - 1) * 2;
            enemy.spd += (battleCount - 1) * 1;
        }


        //int playerTurnFirst = (player.spd >= enemy.spd) ? 1 : 0;       //v.1.2에서 행동게이지 시스템 도입으로 폐기

        double playerGauge = 0.0;       //각 행게 초기화
        double enemyGauge = 0.0;
        double gaugeThreshold = 100.0; // 행동권 기준치

        playerGauge = player.spd * 5.0;     //전투시작시 속도에 비례해 초기 게이지 부여
        enemyGauge = enemy.spd * 5.0;

        printf("\n\n가지고 있는 아이템 목록:\n");     //첫 전투 전 아이템 목록
        applyPassiveItemsToPlayer_fixed(&player);
        pauseLine();

        drawBattleUI(&player, &enemy, playerGauge, enemyGauge, 1);

        if (strstr(enemy.name, "보스") == NULL) {
            printf("\n      %s(이)가 튀어나왔다!\n", enemy.name);
        }
        else {
            printf("\n      보스가 위압감을 뿜어내며 다가온다..\n");
        }
        Sleep(1500);
        while (player.hp > 0 && enemy.hp > 0) {

            while (playerGauge < gaugeThreshold && enemyGauge < gaugeThreshold) {       // 게이지 충전
                playerGauge += player.spd * 0.5;
                enemyGauge += enemy.spd * 0.5;
            }

            if (playerGauge >= gaugeThreshold) {        // 플레이어 행동

                if (hasBlessingById("gen_getHP20")) {       // 턴 시작 회복
                    int heal = 10;
                    player.hp += heal;
                    if (player.hp > player.maxHp) player.hp = player.maxHp;
                    textcolor(10);
                    printf("턴 시작 회복 효과로 HP %d 회복!\n", heal);
                    textcolor(15);
                    Sleep(500);
                }
                if (getEffectRemaining("energy") > 0) {     // 에너지 드링크 회복
                    int heal = (int)(player.maxHp * 0.30 + 0.0001);
                    player.hp += heal;
                    if (player.hp > player.maxHp) player.hp = player.maxHp;
                    textcolor(10);
                    printf("에너지 드링크 효과로 HP %d 회복! (남은 턴: %d)\n", heal, getEffectRemaining("energy"));
                    textcolor(15);
                    Sleep(500);
                }

                drawBattleUI(&player, &enemy,playerGauge, enemyGauge, 1);
                player.isDefending = 0; // 내 턴이 오면 방어 태세 해제

              
                int valid = 0;
                while (!valid) {
                    printf("행동을 선택하세요 : ");
                    fgets(buf, sizeof(buf), stdin);
                    int choice = 1;
					if (sscanf(buf, "%d", &choice) != 1)        // 입력 오류 처리
                        choice = 0;

                    switch (choice) {
                    case 1:
                        doAttack(&player, &enemy, &player);
                        valid = 1;
                        break;
                    case 2:
                        doDefend(&player);
                        if (player.isDefending) valid = 1; // 쿨다운 중이면 valid 안 됨
                        break;
                    case 3:
                        showInventoryMenu(&player);
                        if (getEffectRemaining("chip_win") > 0) enemy.hp = 0;
                        drawBattleUI(&player, &enemy,playerGauge, enemyGauge, 1);
                        valid = 1;
                        break;
                    default:
                        printf("잘못된 입력입니다.\n");
                        break;
                    }
                }
                Sleep(700);

                playerGauge -= gaugeThreshold; // 행동게이지 100 소모
                tickActiveEffects(&player);    // 지속 턴 효과 감소
                if (player.defenseCooldown > 0)     // 방어 쿨타임 감소
                    player.defenseCooldown--;
                if (enemy.hp <= 0)

                    break; // 적 사망 시 즉시 종료
            }

            if (enemyGauge >= gaugeThreshold && enemy.hp > 0) {
                // else if문으로 구현하지 않았기에 플레이어 행동 후 적 게이지가 100이 넘었다면 바로 적도 행동한다.
                drawBattleUI(&player, &enemy, playerGauge, enemyGauge, 0);
                enemy.isDefending = 0;
                processEnemyAction(&enemy, &player);

                if (hasItemInInventory("ddakpend") && player.hp > 0) {// 딱정벌레 펜던트 힐처리
                    int heal = (int)(player.maxHp * 0.15 + 0.0001);
                    player.hp += heal;
                    if (player.hp > player.maxHp) player.hp = player.maxHp;
                    textcolor(10); printf("딱정벌레 펜던트의 효과로 HP %d 회복!\n", heal); textcolor(15);
                }
                Sleep(700);
                enemyGauge -= gaugeThreshold; // 행동게이지 100 소모

                if (player.hp <= 0)
                    break; // 플레이어가 죽으면 종료
            }
        } 

        revertPassiveItemsFromPlayer_fixed(&player);
        activeEffectCount = 0;
        adrenaline_spd_change = 0;
        forcedHeadsRemaining = 0;

        if (player.hp <= 0) {
            drawBattleUI(&player, &enemy,playerGauge, enemyGauge, 1);
			system("cls");
            printf("\n%s(이)가 쓰러졌습니다", player.name);
            Sleep(400);
            printf(".");
            Sleep(400);
            printf(".");
            Sleep(400);
            printf(".");
            printf(" 진행한 전투 수: %d\n", battleCount);
            Sleep(500);
            printf("\n\r총 가한 데미지:  0");
            for (int i = 0; i < all_dmg; i++) {
                Sleep(10);
                printf("\r총 가한 데미지:  ");
                if (i>= 500)
                    textcolor(13);
                else if (i >= 300)
                    textcolor(10);
                else
                    textcolor(11);
                printf("%d", i + 1);
                textcolor(15);
            }
            Sleep(500);
            printf("\n\n당신의 등급: ");
            Sleep(800);
			textcolor(13);
            if (all_dmg <= 300) {
                printf("F\n\n");
            }
            else if(all_dmg <= 400){
                printf("D\n\n");
            }
            else if (all_dmg <= 500) {
                printf("C\n\n");
            }
            else {
                printf("B-\n\n");
            }
			textcolor(15);
            Sleep(1000);
            break; // 게임 오버
        }
        else {
            drawBattleUI(&player, &enemy,playerGauge, enemyGauge, 1);
            printf("\n 전투에서 승리하였습니다!\n");
            pauseLine();

            if (battleCount == 7) 
                break; 

            for (int i = 0; i < 3; i++) {
                int idx;
                int isDuplicate;
                do {
                    idx = rand() % 14;
                    isDuplicate = 0;
                    for (int j = 0; j < i; j++) {
                        if (pickedIndices[j] == idx) {
                            isDuplicate = 1;
                            break;
                        }
                    }
                    if (isDuplicate == 0 && hasBlessingById(allBless[idx].id)) {
                        isDuplicate = 1;
                    }
                } while (isDuplicate);

                pickedIndices[i] = idx;
                candidates[i] = allBless[idx];
            }

            clearConsole();
            printf("전투 승리 보상 : 축복을 선택하세요.\n");
            for (int i = 0; i < 3; i++) {
                printf("[%d] %s (", i + 1, candidates[i].ext);
                if (strcmp(candidates[i].grade, "일반") == 0) 
                    textcolor(15);
                else if (strcmp(candidates[i].grade, "희귀") == 0) 
                    textcolor(9);
                else if (strcmp(candidates[i].grade, "전설") == 0) 
                    textcolor(14);
                else 
                    textcolor(13);
                printf("%s", candidates[i].grade);
                textcolor(15);
                printf(")\n");
            }
            printf("[0] 받지 않음\n");
            fgets(buf, sizeof(buf), stdin);
            int pick = 0;
            if (sscanf(buf, "%d", &pick) == 1 && pick >= 1 && pick <= 3) {
                applyBlessingSimple(&player, &candidates[pick - 1], allItems);
                printf("선택한 축복이 적용되었습니다: %s\n", candidates[pick - 1].ext);
                pauseLine();
            }
        }
    } 

    if (player.hp > 0) {
        clearConsole();
        system("cls");
        textcolor(14);
        Sleep(500);
        printf("\n\n축하합니다!\n");
        Sleep(500);
        printf("모든 전투에서 승리하였습니다!\n");
        Sleep(500);
        printf("\r총 가한 데미지:  0");
        for (int i = 0; i < all_dmg; i++) {
            Sleep(5);
            printf("\r총 가한 데미지:  ");
            if (i >= 500)
                textcolor(13);
            else if (i >= 300)
                textcolor(10);
            else
                textcolor(11);
			printf("%d", i + 1);
            textcolor(15);
        }
        Sleep(500);
        printf("\n\n당신의 등급: ");
        Sleep(800);
        if (all_dmg >= 900) {
			textcolor(13);
			printf("S+\n\n");
        }
        else if (all_dmg >= 700) {
            textcolor(13);
            printf("S\n\n");
        }
        else if (all_dmg >=600) {
			//노란색
			textcolor(14);
			printf("A+\n\n");
        }
        else if (all_dmg >= 400) {
			textcolor(14);
			printf("A\n\n");
        }
        else if(all_dmg<400)
            textcolor(11);
		    printf("B\n\n");
    }
    textcolor(15);
    printf("게임 종료\n");
    return 0;
}
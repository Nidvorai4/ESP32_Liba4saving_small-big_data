#include "BSY_ESP32_UniversalStorages.h"


unsigned long imAlive;

bool bTest = true;
int iTest =55;
float fTest = 55.0;
char chTest[] = "char test";
struct STest {
    int iTest =55;
    float fTest = 55.0;
    char chTest[10] = "char test";
}sTest;

StorageSmallAkaNVS nvsTest("npspcTest");

struct BigData {
    char dummy[2990]; 
};


/// @brief проверка на стэковерфлоу (версия либы 1.1.0) падала при попытке загрузки списка 50 датчиков
void testStackCrash() {
    BigData bigTest;
    memset(bigTest.dummy, 'A', sizeof(bigTest.dummy)); // Заполним мусором

    Serial.println(">>> Starting CRASH TEST (Save Big Data)...");
    
    // Этот вызов должен привести к Guru Meditation Error или перезагрузке,
    // если в Storage::save() всё еще написано Package<T> pkg;
    bool resBig = nvsTest.save("big_data", bigTest, 1);
    
    Serial.printf("Save big: %s\n", resBig ? "OK" : "---------------------FAIL");

    BigData bigRead;
    if (nvsTest.load("big_data", bigRead, 1)) {
        Serial.println("Load big: OK");
    } else {
        Serial.println("Load big: FAILED");
    }
}

void setup() {
    delay(1000);
    Serial.begin(115200);
    while (!Serial && millis() < 5000); 
    delay(2000);

    Serial.println("\n--- ЭТАП 1: СОХРАНЕНИЕ ---");

    // 1. Сохраняем bool (ключ "bool")
    bool resBool = nvsTest.save("bool", bTest,1);
    Serial.printf("Save bool: %s\n", resBool ? "OK" : "---------------------FAIL");
    delay(1100);

    // 2. Сохраняем int (ключ "int")
    bool resInt = nvsTest.save("int", iTest,1);
    Serial.printf("Save int: %s\n", resInt ? "OK" : "---------------------FAIL");
    delay(1100);

    // 3. Сохраняем float (ключ "float")
    bool resFloat = nvsTest.save("float", fTest,1);
    Serial.printf("Save float: %s\n", resFloat ? "OK" : "---------------------FAIL");
    delay(1100);

    // 4. Сохраняем структуру (ключ "struct", форсированно)
    bool resStruct = nvsTest.save("struct", sTest, 1, true); 
    Serial.printf("Save struct: %s\n", resStruct ? "OK" : "---------------------FAIL");

    Serial.println("\n--- ЭТАП 2: ПРОВЕРКА (ЗАГРУЗКА) ---");

    // ПРОВЕРКА BOOL
    bool bRead = false;
    if (nvsTest.load("bool", bRead,1)) {
        Serial.printf("Load bool OK: %s (Match: %s)\n", 
                      bRead ? "true" : "false", (bRead == bTest ? "YES" : "------------------------NO"));
    } else {
        Serial.println("Load bool: FAILED");
    }

    // ПРОВЕРКА INT
    int iRead = 0;
    if (nvsTest.load("int", iRead,1)) {
        Serial.printf("Load int OK: %d (Match: %s)\n", 
                      iRead, (iRead == iTest ? "YES" : "------------------------NO"));
    } else {
        Serial.println("Load int: FAILED");
    }

    // ПРОВЕРКА FLOAT
    float fRead = 0.0;
    if (nvsTest.load("float", fRead,1)) {
        Serial.printf("Load float OK: %.2f (Match: %s)\n", 
                      fRead, (fRead == fTest ? "YES" : "------------------------NO"));
    } else {
        Serial.println("Load float: FAILED");
    }

    // ПРОВЕРКА СТРУКТУРЫ
    STest sRead;
    if (nvsTest.load("struct", sRead,1)) {
        bool structMatch = (sRead.iTest == sTest.iTest) && 
                           (abs(sRead.fTest - sTest.fTest) < 0.001) && 
                           (strcmp(sRead.chTest, sTest.chTest) == 0);
        
        Serial.printf("Load struct OK! Data: i=%d, f=%.2f, s=%s\n", 
                      sRead.iTest, sRead.fTest, sRead.chTest);
        Serial.printf("Struct Match: %s\n", structMatch ? "YES" : "------------------------NO");
    } else {
        Serial.println("Load struct: FAILED");
    }

    testStackCrash();

    Serial.println("\n--- ТЕСТ ЗАВЕРШЕН ---");


    nvsTest.fullResetNVS();

}

void loop(){
    if (millis() - imAlive > 10000) {
        imAlive = millis();
        Serial.print("im alive " );
        Serial.println(BASAY_UNIVERSALSTORAGES_VERSION);
    }
}
#ifndef PICTUREVIEWPLUGIN_H
#define PICTUREVIEWPLUGIN_H

// Интерфейс PVD плагинов-декодеров, v1.1 2026.09.30

// Кодировка всех char строк: UTF-8.

#ifndef PVDContext
#define PVDContext void
#endif

// Версия интерфейса PVD, описываемая этим заголовочным файлом.
// Именно это значение рекомендуется возвращать в pvdInit при успешной инициализации.
#define PVD_CURRENT_INTERFACE_VERSION 1

// Анимированное многостраничное изображение
#define PVD_IIF_ANIMATED 1

// Есть информация о прозрачности изображения
#define PVD_IDF_ALPHA 2
// Есть профиль ICC
#define PVD_IDF_ICC 4


// pvdInfoPlugin - информация о плагине
typedef struct pvdInfoPlugin
{
	UINT32 Priority;          // приоритет плагина; если 0, то плагин не будет вызываться в процессе автораспознавания
	const char *pName;        // имя плагина
	const char *pVersion;     // версия плагина
	const char *pComments;    // комментарии о плагине: для чего предназначен, кто автор плагина, ...
} pvdInfoPlugin;

// pvdInfoImage - информация о файле
typedef struct pvdInfoImage
{
	UINT32 nPages;            // количество страниц изображения
	UINT32 Flags;             // возможные флаги: PVD_IIF_ANIMATED
	const char *pFormatName;  // название формата файла
	const char *pCompression; // алгоритм сжатия
	const char *pComments;    // различные комментарии о файле
} pvdInfoImage;

// pvdInfoPage - информация о странице изображения
typedef struct pvdInfoPage
{
	UINT32 lWidth;            // ширина страницы
	UINT32 lHeight;           // высота страницы
	UINT32 nBPP;              // количество бит на пиксель (только информационное поле - в обработке не используется)
	UINT32 lFrameTime;        // для анимированных изображений - длительность отображения страницы в тысячных секунды,
	                          // иначе - не используется
} pvdInfoPage;

// pvdInfoDecode - информация о декодированном изображении
typedef struct pvdInfoDecode
{
	BYTE   *pImage;            // указатель на данные изображения в формате BGR, RGB, Y или Index; little-endian
	UINT32 *pPalette;          // указатель на палитру изображения, используется в форматах 8 и меньше бит на пиксель
	UINT32 Flags;              // возможные флаги: PVD_IDF_ALPHA, PVD_IDF_ICC
	UINT32 nBPP;               // количество бит на пиксель в данных, на которые указывает pImage:
                               // 1, 2, 4, 8 - серые или палитровые изображения
                               // 16 - серые изображения
                               // 15, 17 - BGR555, BGR565
                               // 24, 32, 48, 64 - BGR/BGRA/BGRX 8/16 бит
                               // 25, 33, 49, 65 - RGB/RGBA/RGBX 8/16 бит
	UINT32 nColorsUsed;        // количество используемых цветов в палитре; если 0, то используются все возможные цвета
	INT32  lImagePitch;        // модуль - длина строки декодированного изображения в байтах;
	                           // положительные значения - строки идут сверху вниз, отрицательные - снизу вверх
	// расширение: ICC профиль, поля заполняются только в случае установленного флага PVD_IDF_ICC
	BYTE   *pICC;              // указатель на данные ICC
	UINT32 lICC;               // длина данных ICC
} pvdInfoDecode;


// pvdDecodeCallback - функция, указатель на которую передаётся в pvdPageDecode
//  Вызывается: плагином из pvdPageDecode
//   Не обязательно, но рекомендуется периодически вызывать, если декодирование может занять длительное время.
//  Аргументы:
//   pDecodeCallbackContext - контекст, переданный соответствующим параметром pvdPageDecode
//   iStep  - номер текущего шага декодирования (нумерация от 0 до nSteps - 1)
//   nSteps - общее количество шагов декодирования
//  Возвращаемое значение: TRUE - продолжение декодирования; FALSE - декодирование следует прервать
typedef BOOL (__stdcall *pvdDecodeCallback)(void *pDecodeCallbackContext, UINT32 iStep, UINT32 nSteps);


// pvdInit - инициализация плагина
//  Вызывается: один раз - сразу после загрузки плагина
//  Возвращаемое значение: версия интерфейса плагина
//   Если это число не понравится вызывающей программе, то вызовется pvdExit и плагин будет выгружен.
//   Рекомендуется возвращать PVD_CURRENT_INTERFACE_VERSION.
//   0 - ошибка загрузки/инициализации плагина.
UINT32 __stdcall pvdInit(void);

// pvdExit - завершение работы с плагином
//  Вызывается: один раз - непосредственно перед выгрузкой плагина
void __stdcall pvdExit(void);

// pvdPluginInfo - общая информация о плагине
//  Вызывается: когда угодно
//  Аргументы:
//   pPluginInfo - указатель на структуру с информацией о плагине для заполнения плагином
void __stdcall pvdPluginInfo(pvdInfoPlugin *pPluginInfo);

// pvdFileOpen, pvdFileOpenW - открытие файла: плагин решает, хочет ли он декодировать файл, и заполняет общую информацию о файле
//  Достаточно реализовать один вариант, предпочитается pvdFileOpenW
//  Вызывается: при открытии файла
//  Аргументы:
//   pFileName   - имя открываемого файла
//   lFileSize   - длина открываемого файла в байтах. Если 0, то файл отсутствует, а переданный параметром pBuf буфер
//                 содержит все возможные данные и будет доступен вплоть до вызова pvdFileClose.
//   pBuf        - буфер, содержащий начало открываемого файла
//   lBuf        - длина буфера pBuf в байтах. Рекомендуется предоставлять не менее 16 Кб.
//   pImageInfo  - указатель на структуру с информацией о файле для заполнения плагином, если он может декодировать файл
//   ppContext   - указатель на контекст. Через этот параметр плагин может вернуть контекст - произвольное значение,
//                 которое будет передаваться ему при вызове других функций работы с данным файлом.
//                 Следует иметь в виду, что одним экземпляром плагина в один момент времени может декодироваться несколько файлов,
//                 поэтому рекомендуется использовать контекст, а не внутренние глобальные переменные плагина.
//  Возвращаемое значение: TRUE - если плагин может декодировать указанный файл; иначе - FALSE
BOOL __stdcall pvdFileOpen(const char *pFileName, INT64 lFileSize, const BYTE *pBuf, UINT32 lBuf, pvdInfoImage *pImageInfo, PVDContext **ppContext);
BOOL __stdcall pvdFileOpenW(const wchar_t *pFileName, INT64 lFileSize, const BYTE *pBuf, UINT32 lBuf, pvdInfoImage *pImageInfo, PVDContext **ppContext);

// pvdPageInfo - информация о странице изображения
//  Вызывается: между удачным pvdFileOpen и pvdFileClose
//  Аргументы:
//   pContext    - контекст, возвращённый плагином в pvdFileOpen
//   iPage       - номер страницы изображения (нумерация начинается с 0)
//   pPageInfo   - указатель на структуру с информацией о странице изображения для заполнения плагином
//  Возвращаемое значение: TRUE - при успешном выполнении; иначе - FALSE
BOOL __stdcall pvdPageInfo(PVDContext *pContext, UINT32 iPage, pvdInfoPage *pPageInfo);

// pvdPageDecode - декодирование страницы изображения
//  Вызывается: между удачным pvdFileOpen и pvdFileClose
//  Аргументы:
//   pContext       - контекст, возвращённый плагином в pvdFileOpen
//   iPage          - номер страницы изображения (нумерация начинается с 0)
//   pDecodeInfo    - указатель на структуру с информацией о декодированном изображении для заполнения плагином
//   DecodeCallback - указатель на функцию, через которую плагин может информировать вызывающую программу о ходе
//                    декодирования; NULL, если такая функция не предоставляется
//   pDecodeCallbackContext - контекст, передаваемый в DecodeCallback
//  Возвращаемое значение: TRUE - при успешном выполнении; иначе - FALSE
BOOL __stdcall pvdPageDecode(PVDContext *pContext, UINT32 iPage, pvdInfoDecode *pDecodeInfo, pvdDecodeCallback DecodeCallback, void *pDecodeCallbackContext);

// pvdPageFree - освобождение декодированного изображения
//  Вызывается: после удачного pvdPageDecode, когда декодированное изображение больше не нужно
//  Аргументы:
//   pContext    - контекст, возвращённый плагином в pvdFileOpen
//   pDecodeInfo - указатель на структуру с информацией о декодированном изображении, заполненной в pvdPageDecode
void __stdcall pvdPageFree(PVDContext *pContext, pvdInfoDecode *pDecodeInfo);

// pvdFileClose - закрытие файла
//  Вызывается: после удачного pvdFileOpen, когда файл больше не нужен
//  Аргументы:
//   pContext    - контекст, возвращённый плагином в pvdFileOpen
void __stdcall pvdFileClose(PVDContext *pContext);

#endif

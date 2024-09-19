// работа с файловой системой
#include <stdio.h>
#include <stdlib.h>
// асинхронный ввод-вывод
#include <aio.h>

#include <fcntl.h>

#include <unistd.h>
// работа с сигналами
#include <signal.h>

#include <string.h>
// манипуляция ошибками
#include <errno.h>

#include <sys/types.h>

#include <sys/stat.h>

#include <signal.h>

#include <stdint.h>

#include <inttypes.h>
// работа со временем
#include <time.h>



#define OPSnum 20 // максимальное число операций
#define BSZ 409600 // размер блока чтения и записи

struct aio_operation {
    struct aiocb aio; // указатель на структуру aiocb (которая представляет асинхронный ввод-вывод)
    char * buffer; // буфер для данных
    int write_operation; // тип операции
    void * next_operation; // указатель на следующую операцию
    int last; // флаг последней операции
};
struct aio_operation aio_ops[OPSnum]; // Массив aio_ops для хранения операций AIO
const struct aiocb * aiolist[OPSnum]; // массив aiolist, который будет содержать указатели на текущие операции

// счетчик numop для отслеживания текущего количества активных операций
// Файловые дескрипторы hFile1 и hFile2 для входного и выходного файлов соответственно
int hFile1, hFile2, numop = 0;

// обработчик завершения AIO 
// аргумент sigval_t - специальная структура для сигналов, используемых в асинхронном вводе-выводе.
void aio_completion_handler(sigval_t sigval) {
    struct aio_operation * aio_op = (struct aio_operation * ) sigval.sival_ptr;
    int n;
    // Если операция отвечает за запись (write_operation == 1), она переключает файловый дескриптор на hFile1 и обновляет состояние.
    if (aio_op -> write_operation == 1) {
        aio_op -> aio.aio_fildes = hFile1;
        aio_op -> write_operation = 2;
        numop--;
    // Если это операция чтения (write_operation == 0), то она обновляет файловый дескриптор на hFile2, устанавливает 
    // новое состояние и пытается выполнить aio_return для получения результата операции. Также фиксирует случаи короткого чтения.
    } else if (aio_op -> write_operation == 0) {
        aio_op -> aio.aio_fildes = hFile2;
        aio_op -> write_operation = 1;
        if ((n = aio_return( & aio_op -> aio)) < 0) {
            printf("aio_return failed\n");
        }
        if (n != BSZ && ! & aio_op -> last)    printf("short read (%d/%d)\n", n, BSZ);
        aio_op -> aio.aio_nbytes = n;
        aio_write( & aio_op -> aio);
    }
}



int main() {
    struct stat FileInfo; //  структура для хранения информации о файле.
    int n;

    // Открываются два файла: один для чтения, другой для записи (создается, если не существует).
    // NONBLOCK - операция файла не блокирующая, т.е. программа будет выполнять другие задачи
    // TRUNC - очищение файла
    // 0666 - права доступа к файлу
    hFile1 = open("fileON.txt", O_RDONLY | O_NONBLOCK, 0666);
    hFile2 = open("fileTO.txt", O_CREAT | O_WRONLY | O_TRUNC | O_NONBLOCK, 0666);


    // Используется для получения информации о файле, по его дескриптору
    // Она предоставляет доступ к метаданным о файле, таким как его размер, права доступа, тип, временные метки и многое другое.
    fstat(hFile1, &FileInfo);


    // Цикл для инициализации массива aio_ops, выделение памяти для буферов и настройка событий для обработки завершения операции.
    for (int i = 0; i < OPSnum; i++) // OPSnum - общее число асинхронных операций
    {
        aio_ops[i].buffer = (char *)malloc(BSZ); // инициализация буфера для хранения асинхронной операции
        if (aio_ops[i].buffer == NULL)    perror("malloc"); 
        aio_ops[i].aio.aio_buf = aio_ops[i].buffer; 
        aio_ops[i].aio.aio_nbytes = BSZ; 
        aio_ops[i].aio.aio_sigevent.sigev_notify = SIGEV_THREAD;
        aio_ops[i].aio.aio_sigevent.sigev_notify_function = aio_completion_handler; 
        aio_ops[i].aio.aio_sigevent.sigev_value.sival_ptr = (void*)&aio_ops[i]; 
        aio_ops[i].write_operation = 2;
        aiolist[i] = NULL;
    }

    // Переменные off для отслеживания текущей позиции в файле, и start для измерения времени выполнения программы.
    off_t off = 0;
    clock_t start = clock();

    for (;;){
        // Вложенный цикл для обработки каждой операции
        for (int i = 0; i < OPSnum; i++)
        {
        int err, n;
        // Если операция write_operation равна 2, выполняется чтение данных из файла. Позиция в файле обновляется, и если достигнут 
        // конец файла, устанавливается флаг last. Операция aio_read выполняется для чтения данных.
        if (aio_ops[i].write_operation == 2)
        {
            if (off < FileInfo.st_size)
            {
                aio_ops[i].write_operation = 0; 
                aio_ops[i].aio.aio_fildes = hFile1; 
                aio_ops[i].aio.aio_offset = off; 
                off += BSZ;
                if (off >= FileInfo.st_size)    aio_ops[i].last = 1; 
                aio_ops[i].aio.aio_nbytes = BSZ; 
                aio_read(&aio_ops[i].aio); 
                aiolist[i] = &aio_ops[i].aio; 
                numop++;
            }
        }
        // Обработка чтения завершенной операции. Проверяется наличие ошибок и результат операции.
        else if (aio_ops[i].write_operation == 0)
        {
            if((err = aio_error(&aio_ops[i].aio)) == EINPROGRESS) continue;
            if(err != 0){
                if(err == -1)    printf("aio_error failed\n");
                else
                    printf("read failed, errno = %d\n", err);
                    continue;
            }
            if((n = aio_return(&aio_ops[i].aio)) < 0){ 
                printf("aio_return failed\n"); continue;
            }
            if(n != BSZ && !&aio_ops[i].last)    printf("short read (%d/%d)\n", n, BSZ);
        }
        // Проверка операций записи. Подход аналогичен чтению, включая обработку ошибок и запись результатов.
        else if (aio_ops[i].write_operation == 1)
        {
            if((err = aio_error(&aio_ops[i].aio)) == EINPROGRESS)    continue;
            if(err != 0){
                if(err == -1)    printf("aio_error failed\n");
                else
                    printf("write failed, errno = %d\n", err);
                    continue;
            }
            if((n = aio_return(&aio_ops[i].aio)) < 0){ 
                printf("aio_return failed\n"); 
                continue;
            }
            if(n != aio_ops[i].aio.aio_nbytes)    printf("short write (%d/%d)\n", n, BSZ); 
            aiolist[i] = NULL;
        }
        if (numop == 0)
        {


        }
        else
        {

        }
    }
    if (off >= FileInfo.st_size)    break;
    aio_suspend(aiolist, OPSnum, NULL);
    }
}


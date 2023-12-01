#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_FILENAME_LENGTH 100
#define MAX_FILES 100

struct File {
    char fileName[MAX_FILENAME_LENGTH];
    mode_t mode;
    off_t size; // 0: No file, > 0: Yes file
    off_t start;
    off_t end;
    int deleted;//0: No, 1: Yes
};

struct Header{
    struct File fileList[MAX_FILES];
} header; // declaration of header

struct BlankSpace{
    off_t start;
    off_t end;
    int index;
    struct BlankSpace * nextBlankSpace;
} * firstBlankSpace; // declaration of blank spaces

off_t currentPosition = 0; // Tracks the current position in tar file
int numFiles=0;

/*
    Function to open or create a file.
    fileName is the name of the file to open or create.
    option is 0 for open and 1 for create.
    if 1, the file is created empty.
    if 0 the file is opened in read and write mode without deleting its contents.
*/
int openFile(const char * fileName,int option){
    int fd;
    if (option==0){ // Reading and writing without deleting content
        fd = open(fileName, O_RDWR  | O_CREAT, 0666);
        if (fd == -1) {
            perror("Error creating the packed file");
            exit(1);
        }
        //printf("File opened in option: 0 -> O_RDWR  | O_CREAT\n");
        return fd;
    }else if (option==1){ // Create empty file
        fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd == -1) {
            perror("Error creating the packed file");
            exit(1);
        }
        //printf("File opened in option: 1 -> O_WRONLY | O_CREAT | O_TRUNC\n");
        return fd;
    }else if (option==2){ //Extend the file (for the body)
        fd = open(fileName, O_WRONLY | O_APPEND, 0666);
        if (fd == -1) {
            perror("Error creating the packed file");
            exit(1);
        }
        //printf("File opened in option: 2 -> O_WRONLY | O_APPEND\n");
        return fd;
    }else{
        printf("File creation/opening option wrong...\n");
        return -1;
    }
}

/*
    Function that receives the name of a file and returns its size
*/
off_t getFileSize(const char * fileName){
    int file = openFile(fileName,0);
    off_t sizeOfFile = lseek(file, 0, SEEK_END); // Obtains size of file
    if (sizeOfFile == -1) {
        perror("getFileSize: Error getting size of file.");
        close(file);
        exit(1);
    }
    close(file);
    return sizeOfFile;
}

/*
    Returns the size in bytes of the sum of the sizes of the files contained in the tar file.
*/
off_t getSizeOfContents(){
    off_t totalSum = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (header.fileList[i].size!=0){
            totalSum += header.fileList[i].size;
        }
    }
    return totalSum;
}

/*
    Function to save a file in header.
    CAUTION: Only saves in NULL positions of the Files array. Checks from start to finish.
*/
void addFileToHeaderFileList(struct File newFile) {
    printf("Adding \"%s\" to header's file list.\n", newFile.fileName);
    for (int i = 0; i < MAX_FILES; i++) {
        if (header.fileList[i].size==0){ // Empty position
            header.fileList[i]= newFile;
            numFiles++;
            break;
        }
    }
}

/*
    Function to find the index of the last file in the header.
    Returns the array index of that last file.
    Returns -1 if there are no files.
*/
int findIndexLastFileInHeader(){
    for (int i = MAX_FILES-1; i >= 0; i--) 
        if (header.fileList[i].size !=  0) 
            return i;
    return -1;
} 

/*
    Function to add a file as last in header.
    NOT as the last of the array, but that this new file is the last in the list of files.
*/
void addFileToHeaderListInLastPosition(struct File newFile){
    int lastPosition = findIndexLastFileInHeader();
    if (lastPosition==-1){
        printf("There are no files.\n");
        header.fileList[0] = newFile;
        return;
    }
    if (lastPosition==MAX_FILES-1){
        printf("No space in header.\n");
        exit(1);
    }
    header.fileList[lastPosition+1] = newFile;
}

/*
    Function to count how many files there are in tar.
    Returns the amount of files in the tar.
*/
int sumFiles(){
    for (int i =0; i<MAX_FILES ; i++){
        if (header.fileList[i].size!=0)
            numFiles++;
    }
    return numFiles;
}

/*
    Function that modifies the start and end position of the existent files.
    It reallocates the start and end position of each file in way that these positions are secuencials, and so are the files.
    Returns an array of the new files information.
*/
struct File * modifiedExistentFiles(int sumFiles){
    struct File* modifiedFiles = (struct File*)malloc(sumFiles * sizeof(struct File));
    int bytePosition = 0;
    int selectedCount = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (header.fileList[i].size != 0){
            struct File modifiedFile = header.fileList[i];
            // New positions
            if (bytePosition == 0) modifiedFile.start = bytePosition = sizeof(header)+1;
            else modifiedFile.start = currentPosition;
            
            modifiedFile.end = currentPosition = modifiedFile.start + modifiedFile.size;
            
            modifiedFiles[selectedCount] = modifiedFile;
            selectedCount++;
        }
    } 
    return modifiedFiles;
}

/*
    Changes the size of a file to the indicated size.
*/
void truncateFile(const char * fileName, off_t newSize){
    int file = openFile(fileName, 0);
    if (file == -1) {
        perror("truncateFile: Error opening file.");
        exit(1);
    }
    
    // Uses ftruncate to change the size
    if (ftruncate(file, newSize) == -1) {
        perror("truncateFile: Error changing the size of the file.");
        close(file);
        exit(1);
    }
    close(file);
}

/*  
    Function to read the header from the tar file.
    Receives the indentifier of the tar file from which the header should be read.
    Returns 1 if read correctly.
    DOES NOT close the tar file.
*/
int readHeaderFromTar(int tarFile){
    if (lseek(tarFile, 0, SEEK_SET) == -1) {
        perror("readHeaderFromTar: Error positioning the pointer at the beginning of the file.");
        close(tarFile);
        exit(1);
    }
    char headerBlock[sizeof(header)]; // Should be big enough to contain the header
    ssize_t bytesRead = read(tarFile, headerBlock, sizeof(headerBlock));
    if (bytesRead < 0) {
        perror("readHeaderFromTar: Error reading header from tar file.");
        exit(1);
    } else if (bytesRead < sizeof(header)) {
        fprintf(stderr, "readHeaderFromTar: It was not possible to read all the header from tar file.\n");
        exit(1);
    }
    memcpy(&header, headerBlock, sizeof(header)); // Copies the content to the 'header' struct.
    return 1;
}

/*
    Function to read the content of a file in the tar.
    Returns the content in a buffer.
    If error, returns NULL
*/
char * readContentFromTar(int tarFile, struct File fileToRead){
    if(lseek(tarFile, fileToRead.start, SEEK_SET) == -1){ // Puts pointer of the file tar at the start of the content to be extracted
        perror("readContentFromTar: Error positioning the pointer at the beginning of the file to be extracted.");
        close(tarFile);
        return NULL;
    }

    char* buffer = (char*)malloc(fileToRead.size); // To store content

    if (read(tarFile, buffer, fileToRead.size) == -1){ // Reads content in 'buffer'
        perror("readContentFromTar: Error reading the file content from tar.");
        free(buffer); // liberates buffer
        close(tarFile);
        return NULL;
    }

    close(tarFile);
    return buffer;
}

/*
    Function to find a file in the tar file.
    Returns a file struct.
    If not found, the file returned has size = 0;
*/
struct File findFile(const char * tarFileName,const char * fileName){
    int tarFile = openFile(tarFileName,0);
    if (readHeaderFromTar(tarFile)==1){
        for (int i = 0; i < MAX_FILES; i++) {
            if (strcmp(header.fileList[i].fileName,fileName)==0){//Encontro el archivo
                close(tarFile);
                return header.fileList[i];
            }
        }
    }else{
        printf("findFile: Error reading header from tar.\n");
        close(tarFile);
        exit(10);
    }
    close(tarFile);
    printf("findFile: File not found.\n");
    struct File notFound;
    notFound.size=0;
    return notFound;
}

/*
    Functino to find the position of a file in the tar header.
    Returns -1 if the file is not found.
    Returns the index of the file from the file list in header.
*/
int findIndexFile(const char * tarFileName,const char * fileName){
    int tarFile = openFile(tarFileName,0);
    if (readHeaderFromTar(tarFile)==1){
        for (int i = 0; i < MAX_FILES; i++) {
            if (strcmp(header.fileList[i].fileName,fileName)==0){ // File found
                close(tarFile);
                return i;
            }
        }
    }else{
        printf("findFile: Error 1 reading header from tar.\n");
        close(tarFile);
        exit(10);
    }
    close(tarFile);
    printf("findFile: Error file not found.\n");
    return -1;
}

/*
    Function to print header
*/
void printHeader(){
    printf("\nHEADER: \n");
    for (int i = 0; i < MAX_FILES; i++) {
        if (header.fileList[i].size !=  0) {
            printf("File name: %s \t Index:%i \t Size: %lld \t Start: %lld \t End: %lld\n", header.fileList[i].fileName,i, header.fileList[i].size, header.fileList[i].start, header.fileList[i].end);
        }
    }
    printf("\n");
}

/*
    Function to obtain the last file in the header list.
    Returns the last file in the header list.
    If there are no files, returns an empty struct.
*/
struct File findLastFileInHeader(){
    for (int i = MAX_FILES-1; i >= 0; i--) 
        if (header.fileList[i].size !=  0) 
            return header.fileList[i];
    return header.fileList[0]; // Empty struct. There are no elements on the list.
}


/*
    Function to evaluate if a blank space node is repeated.
    Returns 1 if repeated. Otherwise, returns 0.
*/
int isBlankSpaceRepeated(off_t start, off_t end){
    struct BlankSpace * actual = firstBlankSpace;
    while (actual!=NULL){
        if (actual->start == start && actual->end == end)
            return 1;
        actual=actual->nextBlankSpace;
    }
    return 0;
}

/*
    Function to add a blank space to BlankSpaceList.
    The first element (firstBlankSpace) is declared at the struct definition.
    Blank space goes from 'start' to 'end' parameters.
    Returns 1 if added correctly.
*/
int addBlankSpace(off_t start, off_t end, int index){
    if (isBlankSpaceRepeated(start,end)==1){
        return 0;
    }   
    if (start > end) {
        fprintf(stderr, "addBlankSpace: Error Start of blank space is higher than end.\n");
        exit(1);
    }
    if (start==0 && end==0)
        return 0;
    
    // Create a new node
    struct BlankSpace * newBlankSpace = (struct BlankSpace*)malloc(sizeof(struct BlankSpace));
    if (newBlankSpace == NULL) {
        fprintf(stderr, "addBlankSpace: Error Malloc for new node.\n");
        exit(1);
    }
    // Initialize new node of blank space;
    newBlankSpace->start = start;
    newBlankSpace->end = end;
    newBlankSpace->index = index;
    newBlankSpace->nextBlankSpace = NULL;

    // Empty list or new node goes before the first one: Index lower.
    if (firstBlankSpace == NULL || newBlankSpace->index < firstBlankSpace->index) {
        newBlankSpace->nextBlankSpace = firstBlankSpace; // Adds node at the start of the list
        firstBlankSpace = newBlankSpace;
    } else { // List with elements
        struct BlankSpace * current = firstBlankSpace;
        
        // Iterates while there is a next one and the index of the new node is higher or equal than current.
        while ((current->nextBlankSpace != NULL) &&
              (current->nextBlankSpace->index <= newBlankSpace->index)) {
            current = current->nextBlankSpace;
        }

        newBlankSpace->nextBlankSpace = current->nextBlankSpace;
        current->nextBlankSpace = newBlankSpace;
    }
    return 1;
}

/*
    Function to delete a blank space from the list.
    targetIndex is the index of the blank space to be eliminated
*/
void deleteBlankSpace(int targetIndex) {
    struct BlankSpace * current = firstBlankSpace;
    struct BlankSpace * prev = NULL;

    // Search for the element that matches the target index
    while (current != NULL && current->index != targetIndex) {
        prev = current; // Leaves pointer in the previous element.
        current = current->nextBlankSpace; // Advances
    }
    
    if (current == NULL) { // Element not found
        printf("deleteBlankSpace: Element not found with index %d\n", targetIndex);
        return;
    }
    // Adjuste pointers to delete de element

    if (prev == NULL){ // If prev is NULL, it never got in the while cycle, so current is the first element of the list.
        firstBlankSpace = current->nextBlankSpace; // The element to be deleted is the first in the list.
    }else{ // Not the first element.
        prev->nextBlankSpace = current->nextBlankSpace;
    }
    // Liberar la memoria del elemento eliminado
    free(current);
}

/*
    Resets and cleans completely the blank spaces list.
*/
void resetBlankSpaceList() {
    struct BlankSpace * current = firstBlankSpace;
    while (current != NULL) {
        struct BlankSpace *next = current->nextBlankSpace;
        free(current); // Libera la memoria del nodo actual
        current = next;
    }
    firstBlankSpace = NULL; // Establish pointer to null to indicate that the list is empty.
}

/*
    Function to print the current blank spaces.
*/
void printBlankSpaces(){
    printf("\nBLANK SPACES: \n");
    struct BlankSpace * current = firstBlankSpace;
    if (current==NULL){
        printf("There are no blank spaces\n");
        return;
    }
    while (current != NULL) {
        printf("BlankSpace ->\tStart: %lld\tEnd: %lld\n", current->start, current->end);
        current = current->nextBlankSpace;
    }
    printf("\n");

}

/*  
    Function to write the header in the tar file.
    tarFile is the indiciator of the tar file. Must be opened in writing mode.
*/
void writeHeaderToTar(int tarFile){
    printf("Writing header to tar...\n");
    char headerBlock[sizeof(header)]; // Block to be written on tar file
    memset(headerBlock, 0, sizeof(headerBlock));  // Empty assurance
    memcpy(headerBlock, &header, sizeof(header)); // Copies content from header on the headerBlock
    if (write(tarFile, headerBlock, sizeof(headerBlock)) != sizeof(headerBlock)){ // Writes headerBlock in tar file
        perror("writeHeaderToTar: Error writing header in tar file.");
        exit(1);
    }
}

/*
    Function to write the content of a file in the tar file.
    tarFileName is the name of the tar file.
    fileName is the name of the file which its content will be recorded on the tar file body.
*/
void writeFileContentToTar(const char * tarFileName,const char * fileName){
    int file = openFile(fileName,0);
    struct File fileInfo = findFile(tarFileName,fileName); // Finds file in the header
    if (fileInfo.size==0){
        printf("writeFileContentToTar: File not found in tar file.\n");
        exit(11);
    }
    char buffer[fileInfo.size]; // Buffer to save the content of the file
    read(file,buffer, sizeof(buffer)); // Reads content and saves it of buffer
    int tarFile = openFile(tarFileName,0);
    lseek(tarFile, fileInfo.start, SEEK_SET); // Sets pointer to the start position for the file

    if (write(tarFile, buffer, sizeof(buffer)) == -1) { // Writes
        perror("writeFileContentToTar: Error writing on tar file.");
        close(tarFile);
        exit(1);
    }
    // Close both files
    close(file);
    close(tarFile);
}

/*
    Function to write the content of the files stored in header into the tar files.
    tarFileName is the name of the tar file.
    fileNames is an array with the names of all the files to be written.
    numFiles is the ammount of files to be written.
*/
void writeBodyToTar(const char * tarFileName,const char * fileNames[],int numFiles){
    printf("Writing body to tar...\n");
    for (int i=0;i<numFiles;i++){
        struct stat fileStat;
        if (lstat(fileNames[i], &fileStat) == -1) { // Extracts file info and saves it on fileStat
            perror("writeBodyToTar: Error getting file info.");
            exit(1);
        }else
            writeFileContentToTar(tarFileName,fileNames[i]);
    }
}

/*
    Function in charge of writing the body of tar file.
    tarFileName is the name of the tar file.
    fileNames is an array with the names of all the files to be written.
    numFiles is the ammount of files to be written.
*/
void createBody(const char * tarFileName, const char *fileNames[],int numFiles){
    int tarFile = openFile(tarFileName,0);
    if (readHeaderFromTar(tarFile)==1){ // Read the content in header to be up to date.
        close(tarFile);
        writeBodyToTar(tarFileName, fileNames,numFiles);
    }else{
        printf("createBody: Error reading header.\n");
        close(tarFile);
        exit(10);
    }
    close(tarFile);
}

/*
    Function to create the tar file header.
    numFiles is the ammount of files to be written.
    tarFile is the file tar that will be created.
    fileNames is an array with the names of all the files to be packaged.
*/
void createHeader(int numFiles,int tarFile, const char * fileNames[]){
    printf("\nCREATE HEADER\n");
    const char * fileName;
    struct stat fileStat; // File info
    struct File newFile; // File to be added
    for (int i=0; i < numFiles; i++){
        fileName = fileNames[i];
        if (lstat(fileName, &fileStat) == -1) { // Extracts file info and saves it on fileStat
            perror("createHeader: Error getting file info.");
            close(tarFile);
            exit(1);
        }
        // Copy file info to struct
        strncpy(newFile.fileName, fileName, MAX_FILENAME_LENGTH);
        newFile.size = fileStat.st_size;
        newFile.mode = fileStat.st_mode;
        newFile.deleted = 0;
        if (currentPosition==0) // First file
            newFile.start = currentPosition = sizeof(header)+1;
        else 
            newFile.start = currentPosition;
        newFile.end = currentPosition = newFile.start + fileStat.st_size;
        addFileToHeaderFileList(newFile); // Update header
    }
    writeHeaderToTar(tarFile); // Writes header on tar file
    close(tarFile);
}

/*
    Function to calculate the blank spaces between the files in the tar file.
    lastFile is the last existent file in the tar file.
    sizeOfTar is the size of the whole tar file.
    tarFileName is the name of the tar file.
*/
void calculateSpaceBetweenFilesAux(struct File lastFile, off_t sizeOfTar, const char * tarFileName){
    struct File nextFile;
    int indexLastFile = findIndexFile(tarFileName,lastFile.fileName);
    for (int i = 0; i < MAX_FILES ; i++ ){
        if (header.fileList[i].deleted == 1){ // This position has been already deleted
            if (i != MAX_FILES-1 && i != 0 ){ // Not the last nor the first file
                if (header.fileList[i+1].start != 0){
                    addBlankSpace(header.fileList[i-1].end, header.fileList[i+1].start, i);
                } else{ // There are not more files
                    addBlankSpace(header.fileList[i-1].end, sizeOfTar-1, i);
                }
            }else if (i==0){ // First file
                addBlankSpace(sizeof(header) + 1 ,header.fileList[i+1].start, 0);
            }
        }else{
            if (i < MAX_FILES-1 && (header.fileList[i+1].size>0) && (header.fileList[i].end != header.fileList[i+1].start)){ // Not the last
                addBlankSpace(header.fileList[i].end, header.fileList[i+1].start, i);
            }
        }
    }
}

/*
    Function called to calculate the blank spaces.
    Prints blank spaces when done.
*/
void calculateBlankSpaces(const char * tarFileName){
    printf("Calculating blank spaces...\n");
    off_t sizeOfTar = getFileSize(tarFileName);// Obtiene el tamaño del archivo tar.
    int tarFile = openFile(tarFileName, 0);// Abre el archivo para leer el header.

    if (readHeaderFromTar(tarFile) != 1){
        printf("calculateBlankSpaces: Error reading header from tar file.\n");
        close(tarFile);
        exit(10);
    }
    close(tarFile);
    calculateSpaceBetweenFilesAux(findLastFileInHeader(),sizeOfTar,tarFileName);
    printBlankSpaces();
}

/*
    Functino that creates a tar file with the selected files.
    fileNames is an array with all the files to be added in the tar file.
    tarFileName is the name of the tar file.
    numFiles is the ammount of files that will be created.
*/
void createStar(int numFiles, const char *tarFileName, const char *fileNames[]){
    printf("\nCREATE TAR FILE\n");
    printf("Size of header: %ld\n",sizeof(header));
    if (numFiles > MAX_FILES){
        printf("createStar: The maximum number of files has been exceeded.\n");
        exit(1);
    }
    createHeader(numFiles,openFile(tarFileName,1),fileNames);
    printHeader();
    createBody(tarFileName,fileNames,numFiles);
}

/*
    Function to delete a file from header.
    file is the file to be deleted.
    Returns 1 if deleted successfully.
*/
int deleteFileFromHeader(struct File file){
    printf("Deleting file from header...\n");
    for (int i=0;i<MAX_FILES;i++){
        if (strcmp(header.fileList[i].fileName,file.fileName)==0){
            header.fileList[i].size=0;
            header.fileList[i].deleted=1; // Used to not mix the blank spaces
            numFiles--;
            return 1;
        }
    }
    return 0;
}

/*
    Function to eliminate the content of a file from the body of the tar file.
    tarFileName is the name of the tar file.
    fileToBeDeleted is the file to be deleted.
    DOES NOT modify the size of the tar file.
*/
void deleteFileContentFromBody(const char* tarFileName,struct File fileToBeDeleted) {
    printf("Deleting file from body...\n");
    FILE * tarFile = fopen(tarFileName, "r+");
    if (tarFile == NULL) {
        perror("deleteFileContentFromBody: Error opening file.");
        exit(1);
    }
    fseek(tarFile, 0, SEEK_END);
    char buffer[1028]; // Default writing size
    fseek(tarFile, fileToBeDeleted.start, SEEK_SET); // Moves pointer to the start of the range

    // Fills the range with null characters
    size_t rangeSize = fileToBeDeleted.end - fileToBeDeleted.start + 1;
    memset(buffer, 0, sizeof(buffer));
    
    // Makes sure to delete all the content although the size of the buffer
    while (rangeSize > 0) {
        size_t bytesToWrite = rangeSize < sizeof(buffer) ? rangeSize : sizeof(buffer);
        fwrite(buffer, 1, bytesToWrite, tarFile);
        rangeSize -= bytesToWrite;
    }
    fseek(tarFile, 0, SEEK_END);
    fclose(tarFile);
}

/*
    Function in charge to delete a file from the tar file.
    tarFileName is the name of the tar file.
    fileNameTobeDeleted is the name of the file to be deleted.
    Returns 0 if successfully.
*/
int deleteFile(const char * tarFileName,const char * fileNameTobeDeleted){
    printf("\nDELETE FILE\n");
    int tarFile = openFile(tarFileName,0);
    struct File fileTobeDeleated = findFile(tarFileName,fileNameTobeDeleted);
    if (fileTobeDeleated.size==0){
        printf("deleteFile: File not found in the tar file.\n");
        exit(11);
    }
    printf("File to be deleted: %s\tStart:%lld\tEnd: %lld\n",fileNameTobeDeleted,fileTobeDeleated.start,fileTobeDeleated.end);

    deleteFileContentFromBody(tarFileName,fileTobeDeleated); // Deletes file from body of tar file.
    deleteFileFromHeader(fileTobeDeleated); // Deletes file from header.
    writeHeaderToTar(tarFile); // Re-writes header to tar
    close(tarFile);
    calculateBlankSpaces(tarFileName); // Re-calculate blank spaces.
    return 0;
}


/*
    Function to list the contents inside the tar file.
    tarFileName is the name of the tar file.
*/
void listStar(const char * tarFileName) {
    int tarFile = openFile(tarFileName,0);
    if (tarFile == -1) {
        fprintf(stderr, "listStar: Error opening tar file.\n");
        exit(1);
    }
    if (readHeaderFromTar(tarFile)!=1){
        printf("listStar: Error reading the header from tar file.\n");
        exit(1);
    }
    printf("\nLIST TAR FILES\n");
    printHeader();
    close(tarFile);
}

/*
    Function that finds an available blank space to place a new file.
    sizeOfNewFile is the size of the new file.
    Returns the found blank space.
    Returns NULL if there are no blank spaces with enough size.
*/
struct BlankSpace * findBlankSpaceForNewFile(off_t sizeOfNewFile){
    struct BlankSpace * current = firstBlankSpace;
    off_t spaceSize;
    while (current != NULL) {
        spaceSize = current->end - current->start;
        if (spaceSize >= sizeOfNewFile)
            return current;
        current = current->nextBlankSpace;
    }
    return NULL; 
}

/* 
    Function to write content at the end of the tar file.
    tarFileName is the name of the tar file.
    fileName is the name of the file to be added.
*/
void writeAtTheEndOfTar(const char * tarFileName ,const char * fileName){
    int tarFile = openFile(tarFileName,0);
    int file = openFile(fileName,0);
    struct stat fileStat;
    if (lstat(fileName, &fileStat) == -1) { // Get info from file to be added.
        perror("append: Error al obtener información del archivo.\n");
        exit(1);
    }
    struct File fileInfo;
    struct File lastFile = findLastFileInHeader();
    
    // Fill info of file in fileInfo
    strncpy(fileInfo.fileName,fileName,MAX_FILENAME_LENGTH);
    fileInfo.mode = fileStat.st_mode; 
    fileInfo.size = fileStat.st_size;
    fileInfo.start = lastFile.end;  
    fileInfo.end = lastFile.end+ fileStat.st_size;
    fileInfo.deleted = 0; 

    addFileToHeaderListInLastPosition(fileInfo); // adds file to header
    writeHeaderToTar(tarFile); // Re-writes header in tar file.

    char buffer[fileInfo.size]; // Buffer to save the content of the file to be saved
    read(file,buffer, sizeof(buffer)); // Saves content file in buffer
    lseek(tarFile, fileInfo.start, SEEK_SET); // Sets pointer to the beginning of the tar file
    if (write(tarFile, buffer, sizeof(buffer)) == -1) {
        perror("writeFileContentToTar: Error writing in the file.");
        close(tarFile);
        exit(1);
    }
    close(file);
    close(tarFile);
}

/*
    Resets the header so it loses all its info and sets to default state.
*/
void resetHeader() {
    for (int i = 0; i < MAX_FILES; i++) {
        memset(header.fileList[i].fileName, 0, MAX_FILENAME_LENGTH); // Empty string
        header.fileList[i].mode = 0; 
        header.fileList[i].size = 0;
        header.fileList[i].start = 0;
        header.fileList[i].end = 0;
        header.fileList[i].deleted = 0;
    }
}


/*
    Function in charge of append the content of a file in the tar file. Must look for available spaces.
    tarFileName is the name of the tar file.
    If the new content does not fit in any space, the packed file is grown.
*/
void append(const char * tarFileName,const char * fileName){
    printf("\nAPPEND\n");
    if (sumFiles() >= MAX_FILES){
        printf("Maximum ammount of files reached.\n");
        exit(1);
    }
    calculateBlankSpaces(tarFileName); // Calculates blank spaces
    struct stat fileStat;
    if (lstat(fileName, &fileStat) == -1) { // Get info from the file to be added
        perror("append: Error al obtener información del archivo.\n");
        exit(1);
    }
    // Search for available space
    struct BlankSpace * availableSpace = findBlankSpaceForNewFile(fileStat.st_size);
    if (availableSpace == NULL){ // If there is no available space, the file is added at the end
        writeAtTheEndOfTar(tarFileName,fileName);
    }else{
        // Updates header
        strncpy(header.fileList[availableSpace->index].fileName,fileName,MAX_FILENAME_LENGTH);
        header.fileList[availableSpace->index].deleted = 0;
        header.fileList[availableSpace->index].start = availableSpace->start;
        header.fileList[availableSpace->index].end = availableSpace->start + fileStat.st_size;
        header.fileList[availableSpace->index].size = fileStat.st_size;
        
        int tarFile = openFile(tarFileName,0);
        writeHeaderToTar(tarFile); // Re-write header in tar
        close(tarFile);
        writeFileContentToTar(tarFileName,fileName); // Write content of the file in the tar file
        deleteBlankSpace(availableSpace->index); // Delete the blank space
    }
    printHeader();
    calculateBlankSpaces(tarFileName);    
}

/*
    Function in charge of extracting the specified files from tar file.
    Reads the content of every file from the tar file and copies the content in a new file with the original name.
    numFiles is the ammount of files to be extracted.
    tarFileName is the name of the tar file.
    fileNames is an array with all the names of the files to be extracted.
*/
void extract(int numFiles, const char *tarFileName, const char *fileNames[]){
    for (int i=0; i<numFiles; i++){
        struct File fileToBeExtracted = findFile(tarFileName, fileNames[i]); // Reads header
        if (fileToBeExtracted.size==0){
            printf("extract: A file does not exist in the tar file.\n");
            exit(11);
        }
        char* content = readContentFromTar(openFile(tarFileName, 0), fileToBeExtracted); // Gets the content of the file from tar

        int extractedFile = openFile(fileToBeExtracted.fileName, 1); // New File
        if (write(extractedFile, content, fileToBeExtracted.size) == -1){
            perror("Error al escribir en el archivo de salida.");
            free(content);
            close(extractedFile);
            exit(1);
        }
        printf("File \"%s\" extracted in execution directory.\n", fileToBeExtracted.fileName);
        free(content); // Liberates buffer
        close(extractedFile);
    }
    printHeader();

}

/*
    Functino that extracts the content of all the files in the tar file.
    Reads the content of every file from the tar file and copies the content in a new file with the original name.
    tarFileName is the name of the tar file.
*/
void extractAll(const char *tarFileName){
    int tarFile = openFile(tarFileName,0);
    if (readHeaderFromTar(tarFile)!=1){
        printf("extractAll: Error reading the header from tar.\n");
        close(tarFile);
        exit(10);
    }
    close (tarFile);
    for (int i = 0; i < MAX_FILES; i++) {
        if (header.fileList[i].size!=0){ // Found a file
            struct File fileToBeExtracted = header.fileList[i];
            char* content = readContentFromTar(openFile(tarFileName, 0), fileToBeExtracted); // Gets content
            int extractedFile = openFile(fileToBeExtracted.fileName, 1); // New File
            if (write(extractedFile, content, fileToBeExtracted.size) == -1){
                perror("Error al escribir en el archivo de salida.");
                free(content);
                close(extractedFile);
                exit(1);
            }
            printf("File \"%s\" extracted in execution directory.\n", fileToBeExtracted.fileName);
            free(content); // Liberates buffer
            close(extractedFile);
        }
    }
}

/*
    Function in charge to update the contents of an archive contained in the tar file.
    First it deletes the original content of the mentioned archive.
    Then, it adds the new content of the same file; its position is modified according to the append function.

    tarFileName is the name of the tar file.
    fileToBeUpdatedName is the name of the file to be updated.
*/
void update(const char *tarFileName, const char *fileToBeUpdatedName){
    if (deleteFile(tarFileName, fileToBeUpdatedName) == 0){ // If deleted well, appends.
        append(tarFileName, fileToBeUpdatedName);
    }
}

/*
    Returns an unique character string with all the valid content in the tar file.
    tarFileName is the name of the tar file.
    * fileSum is a pointer received to be modified with the ammount of real files contained in the tar file.
*/
char * getWholeBodyContentInfo(const char * tarFileName, int * filesSum){
    char * wholeContent = (char*)malloc(getSizeOfContents()); // To store all the content
    char * content; // Content of every file
    for (int i=0; i < MAX_FILES; i++){
        if (header.fileList[i].size!=0){
            (*filesSum) += 1;
            content = readContentFromTar(openFile(tarFileName, 0), header.fileList[i]);
            strcat(wholeContent, content);
        }
    }
    free(content);
    return wholeContent;
}

/*
    Function in charge of the defragmentation command. Gets rid of the blank spaces and compresses the tar file.
*/
void pack(const char * tarFileName){
    int tarFile = openFile(tarFileName,0);
    if (readHeaderFromTar(tarFile)!=1){ // Read header from tar
        printf("extractAll: Error reading the header of the tar file.\n");
        close(tarFile);
        exit(10);
    }
    close (tarFile);
    calculateBlankSpaces(tarFileName); // Calculate blank spaces

    printf("PACK\n");
    int sumFiles = 0;
    char * bodyContentBuffer = getWholeBodyContentInfo(tarFileName, &sumFiles); // Char string with all the contents sequentially
    struct File * modifiedFiles = modifiedExistentFiles(sumFiles); // modifies the existent files' start and end position in order to be sequential
    resetHeader(); // Re-starts the header
    for (int j=0; j<sumFiles; j++){
        addFileToHeaderFileList(modifiedFiles[j]); // Adds the files with the new info in the header
    }
    tarFile = openFile(tarFileName,0);
    writeHeaderToTar(tarFile); // Re-write header in tar file
    close(tarFile);
    truncateFile(tarFileName, sizeof(header)); // Truncates the file to only leave the header
    tarFile = openFile(tarFileName, 0);
    if (lseek(tarFile, 0, SEEK_END) == -1) { // Moves pointer to the end of the tar file (the end of the header)
        perror("pack: Error moving the pointer to the end of the tar file.");
        close(tarFile);
        exit(1);
    }
    // Write the content stored in bodyContentBuffer at the end of the tar file
    if (write(tarFile, bodyContentBuffer, strlen(bodyContentBuffer)) == -1) {
        perror("pack: Error writing the content in the tar file.");
        close(tarFile);
        exit(1);
    }
    close(tarFile);
    resetBlankSpaceList(); // Reset blank spaces list
    printHeader();
    printBlankSpaces();
}
int main(int argc, char *argv[]) {//!Modificar forma de usar las opciones
    if (argc < 3) {
        fprintf(stderr, "Use: %s -c|-t|-d|-r|-x|-u|-p <tarFile.tar> [files]\n", argv[0]);
        exit(1);
    }
    const char * opcion = argv[1];
    const char * tarFileName = argv[2];

    // Iterate through all options
    for (int i = 1; i < strlen(opcion); i++) {
        char opt = opcion[i];
        if (opt == 'c'){//* Create
            int numFiles = argc - 3;
            const char * fileNames[MAX_FILES];
            for (int i = 0; i < numFiles; i++) 
                fileNames[i] = argv[i + 3];
            createStar(numFiles, tarFileName, fileNames);
        } 
        else if (opt == 't'){//* List
            listStar(tarFileName);
        } 
        else if (opt == 'd'){//* Delete
            const char * fileName;
            fileName = argv[0 + 3]; // File to be deleted
            deleteFile(tarFileName,fileName);
        }
        else if (opt == 'r'){//* Append
            const char * fileName;
            fileName = argv[0 + 3]; // File to be added
            append(tarFileName,fileName);
        }
        else if (opt == 'x') {//* Extract
            const char * fileNames[MAX_FILES];
            if (argc == 3){ // Extract all
                extractAll(tarFileName);
            } else { // Extract some
                int numFiles = argc - 3;
                for (int i = 0; i < numFiles; i++) 
                    fileNames[i] = argv[i + 3];
                extract(numFiles, tarFileName, fileNames);
            }
        }
        else if (opt == 'u'){//* Update
            const char * fileName;
            fileName = argv[0 + 3]; // File to be updated
            update(tarFileName,fileName);
        }
        else if (opt == 'p'){//* Pack
            pack(tarFileName);
        }else{
            fprintf(stderr, "Uso: %s -c|-t|-d|-r|-x <archivoTar> [archivos]\n", argv[0]);
            exit(1);
        }
    }
    
    
    // Usa la función lseek para mover el puntero al final del archivo
    int tarFile = openFile(tarFileName,0);
    off_t size = lseek(tarFile, 0, SEEK_END);

    if (size == -1) {
        perror("main: Error getting size of tar file.");
        close(tarFile);
        exit(1);
    }

    printf("----------------------------------------------------\n");
    printf("Size of tar file is of: %ld bytes.\n", (long)size);
    printf("PROGRAM ENDS SUCCESSFULLY\n");
    
    return 0;
}
/*本代码作用是快速读取 osd层数据，并保存到U盘中去*/
static int ccc(int argc,char *argv[])
{
    char *infile = NULL;
    infile="/dev/fb0";

    unsigned char *buf=malloc(1280*720*4);
    memset(buf,0,1280*720*4);
    int infd = open(infile, O_RDONLY);
    if(infd <0){
        printf("open fail \n");
    }

    int sizenum = read(infd, buf, 1280*720*4);
    if (sizenum != 1280*720*4) {
        printf("read sizenum =%d\n",sizenum);
    }

    FILE *fp = fopen("/media/sda/1.raw", "wb");
    if (fp == NULL) {
        perror("Failed to open file");
        return 1;
    }

    sizenum = fwrite(buf, 1,1280*720*4, fp);
    if (sizenum != 1280*720*4) {
        printf(" fwrite sizenum =%d\n",sizenum);
    }

    free(buf);
    close(infd);
    fclose(fp);
}

CONSOLE_CMD(ccc, NULL, ccc, CONSOLE_CMD_MODE_SELF, "truncated operation.")

**Linux API 分为三种：**

1）**socket 地址 API：**由一个 IP 地址和端口对（**ip，port**），唯一地表示使用 TCP 通信的一端。也被称为 socket 地址。

2）**socket 基础 API：**socket 的主要 API 都定义在 **sys/socket.h** 文件中，包括创建 socket、命名 socket、监听 socket、接受连接、发起连接、读写数据、获取地址信息、检测带外标记，以及读取和设置 socket 选项。

3）**网络信息 API：**用来实现主机名和 IP 地址之间的转换，以及服务名称和端口号之间的转换。这些 API 被定义在 **netdb.h** 文件中。


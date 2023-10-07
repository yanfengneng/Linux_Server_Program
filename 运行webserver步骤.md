**1）安装 mysql，若是 ubuntu18.04 版本则安装 mysql.5.7，若是 ubuntu20.04版本则安装 mysql8.0 版本即可。**

```mysql
# 安装 mysql 开发库，主要是为了防止 mysql 头文件报错
sudo apt-get install libmysql++-dev

# 更新 apt 软件包
sudo apt-get update

# ubuntu18.04 安装 mysql5.7
sudo apt install mysql-server-5.7
# ubuntu20.04 安装 mysql8.0
sudo apt install mysql-server

# 检查状态
sudo apt install net-tools
sudo netstat -tap | grep mysql

# 查看 mysql 版本
mysql -V

# 查看 mysql默认账号和密码：
sudo cat /etc/mysql/debian.cnf

# 查看mysql状态  
sudo service mysql status
# 启动mysql服务  
sudo service mysql start
# 停止mysql服务 
sudo service mysql stop
# 重启mysql服务 
sudo service mysql restart


#　开启 mysql 服务，只有开启了 mysql 服务才能运行项目
sudo service mysql start

# 进入 mysql
sudo mysql
```






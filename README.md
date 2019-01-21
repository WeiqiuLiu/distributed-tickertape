## Title
Distributed Tickertape system based on RPC and lamport logic clock</br>

基于RPC的分布式股票交易系统，实现lamport逻辑时钟判决分布式系统中事件发生顺序，以确保客户端按相同顺序接受交易信息。

## Usage
### server
./ticker-server 1 [unique server id (int)] localhost [all other servers id]
### client
./ticker-client localhost [server id you want to send] [trades (string)]


## Reference
Leslie Lamport. Time, Clocks, and the Ordering of Events in a Distributed System. In Communications of the ACM, 21(7):558-565, July 1978. (This is the original paper describing logical clocks.)
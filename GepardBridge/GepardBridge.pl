package GepardBridge;

use strict;
use IO::Socket::INET;
use Plugins;
use Log qw(message);
use Globals;

my $server_sock;
my $client_sock;

Plugins::register("GepardBridge", "Gepard 3.0 Bridge for OpenKore", \&on_unload);

# --- ลงทะเบียน Hooks ---
Plugins::addHook("mainLoop_pre", \&check_bridge);
# เพิ่ม Hook สำหรับดักการส่งข้อมูลออก (Bypass Send)
Plugins::addHook("packet_send", \&on_packet_send); 


# เปิด Port 12345 รอการเชื่อมต่อจาก analysis.cpp
sub on_start {
    $server_sock = IO::Socket::INET->new(
        LocalHost => '127.0.0.1',
        LocalPort => '12345',
        Proto => 'tcp',
        Listen => 1,
        Reuse => 1,
        Blocking => 0
    ) or die "Cannot create bridge socket: $!";
    message "[GepardBridge] Waiting for proxy.dll on port 12345...\n", "success";
}

sub check_bridge {
    # 1. รับการเชื่อมต่อจาก proxy.dll
    if (!$client_sock) {
        $client_sock = $server_sock->accept();
        return if !$client_sock;
        $client_sock->blocking(0);
        message "[GepardBridge] proxy.dll (Client) Connected!\n", "success";
    }

    # 2. อ่าน Packet (Clear Text) ที่มาจากตัวเกม -> ส่งเข้า Parser ของบอท
    my $buffer;
    $client_sock->recv($buffer, 2048);
    if (length($buffer) > 0) {
        # หลอกบอทว่าข้อมูลนี้เพิ่งรับมาจาก Server จริงๆ
        $Net::directDevice->{packetParser}->parse($buffer);
    }
}

sub on_packet_send {
    my ($self, $args) = @_;
    
    # ถ้ามีการเชื่อมต่อกับ proxy.dll อยู่
    if ($client_sock) {
        # ส่ง Packet ที่บอทสร้างขึ้น กลับไปที่ proxy.dll (เพื่อส่งเข้า Game Engine)
        $client_sock->send($args->{packet});
        
        # message "[GepardBridge] Redirected Packet to Game: " . unpack("H*", $args->{packet}) . "\n", "info";

        # สั่ง bypass = 1 เพื่อบอก OpenKore ว่า "ไม่ต้องส่งข้อมูลนี้ออก Internet เอง"
        # เพราะถ้าบอทส่งเองจะติด Encryption ของ Gepard ทันที
        $args->{bypass} = 1;
    }
}


sub on_unload {
    $client_sock->close() if $client_sock;
    $server_sock->close() if $server_sock;
}

on_start();
1;
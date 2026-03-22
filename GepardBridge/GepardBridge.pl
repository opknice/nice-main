package GepardBridge;

use strict;
use IO::Socket::INET;
use Plugins;
use Log qw(message);
use Globals;

my $server_sock;
my $client_sock;

Plugins::register("GepardBridge", "Gepard 3.0 Bridge for OpenKore", \&on_unload);
Plugins::addHook("mainLoop_pre", \&check_bridge);
Plugins::addHook_('packet_send', \&on_packet_send);

Plugins::addHook('packet_send', sub {
    my ($self, $args) = @_;
    if ($client_sock) {
        $client_sock->send($args->{packet}); # ส่งกลับไปให้ proxy.cpp (Step 2)
        $args->{bypass} = 1; # สั่ง OpenKore "ไม่ต้องส่งออกเน็ตเอง"
    }
});


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
    message "[GepardBridge] Waiting for analysis.cpp on port 12345...\n", "success";
}

sub check_bridge {
    # 1. รับการเชื่อมต่อจาก DLL (analysis.cpp)
    if (!$client_sock) {
        $client_sock = $server_sock->accept();
        return if !$client_sock;
        $client_sock->blocking(0);
        message "[GepardBridge] analysis.cpp Connected!\n", "success";
    }

    # 2. อ่าน Packet ที่ส่งมาจาก Client (Clear Text)
    my $buffer;
    $client_sock->recv($buffer, 2048);
    if (length($buffer) > 0) {
        # ให้ OpenKore ประมวลผล (อัปเดต HP, ตำแหน่งมอนสเตอร์)
        $Net::directDevice->{packetParser}->parse($buffer);
    }
}

sub on_packet_send {
    my ($self, $args) = @_;
    # Step 2: แทนที่จะส่งออก Internet ให้ส่งกลับไปที่ analysis.cpp (version.dll)
    if ($client_sock) {
        $client_sock->send($args->{packet});
        $args->{bypass} = 1; # บอก OpenKore ว่า "ไม่ต้องส่งออกเน็ตเองนะ"
    }
}


sub on_unload {
    $client_sock->close() if $client_sock;
    $server_sock->close() if $server_sock;
}

on_start();
1;

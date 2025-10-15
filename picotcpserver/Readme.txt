Pico 2W TCP server

Currently functions as independent access point with SSID and Password in main C file

Powershell script to speak with pico for testing

$client = New-Object System.Net.Sockets.TcpClient("192.168.4.1", 4849)
>> $stream = $client.GetStream()
>> $writer = New-Object System.IO.StreamWriter($stream)
>> $reader = New-Object System.IO.StreamReader($stream)
>>
>> # Write something
>> $writer.WriteLine("succesful connection and stuff")
>> $writer.Flush()
>>
>> # Read response
>> $response = $reader.ReadLine()
>> $response

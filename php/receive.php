<?php
	//ini_set('display_errors', 1);

	date_default_timezone_set('Asia/Tokyo');
	header("Content-Type: text/javascript; charset=utf-8");

	require_once("define.php");	// define読み込み


	if ($_SERVER["REMOTE_ADDR"] != ARDUINO_ADDR){

	    $json = "{\"state\":\"error:" . ARDUINO_ADDR . "\"}\n";

	}else{

		$file = './dat/receive.dat';

		file_put_contents($file, date( "c" ), FILE_APPEND | LOCK_EX);
		file_put_contents($file, "\t" . $_GET["data"] . PHP_EOL, FILE_APPEND | LOCK_EX);

		$json = "{\"state\":\"success\"}\n";

	}

	echo $json;
	exit();
?>

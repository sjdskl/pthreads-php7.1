<?php
error_reporting(E_ALL);

class WebRequest extends Thread {
	public $url;
	public $data;
	
	public function __construct($url){
		$this->url = $url;
	}
	
	public function run(){
		if(($url = $this->url)){
			/*
			* If a large amount of data is being requested, you might want to
			* fsockopen and read using usleep in between reads
			*/
			$this->data = file_get_contents($url);
		} else printf("Thread #%lu was not provided a URL\n", $this->getThreadId());
	}
}

$t = microtime(true);
$g = new WebRequest(sprintf("http://www.google.com/?q=%s", rand()*10));
/* starting synchronized */
if($g->start()){
	printf("Request took %f seconds to start ", microtime(true)-$t);
	while($g->isRunning()){
		echo ".";
		$g->synchronized(function() use($g) {
			$g->wait(100);
		});
	}
	if ($g->join()){
		printf(" and %f seconds to finish receiving %d bytes\n", microtime(true)-$t, strlen($g->data));
	} else printf(" and %f seconds to finish, request failed\n", microtime(true)-$t);
	
}
?>

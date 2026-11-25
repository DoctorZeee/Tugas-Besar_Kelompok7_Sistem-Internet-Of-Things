<?php

namespace App\Services;

use RuntimeException;

class LiftUnavailableException extends RuntimeException
{
    public function __construct(string $message, public int $status = 409)
    {
        parent::__construct($message);
    }
}

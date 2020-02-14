//
//  extfs_ios_helper.mm
//  BasiliskII
//
//  Created by rakslice on 2020-02-13.
//  Copyright © 2020 namedfork. All rights reserved.
//

#import <Foundation/Foundation.h>

#import "extfs_ios_helper.h"

const char * getAltTempDirectory() {
    return [NSTemporaryDirectory() UTF8String];
}


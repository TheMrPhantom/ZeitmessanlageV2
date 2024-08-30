import { Stack, Typography } from '@mui/material'
import React from 'react'
import style from './print.module.scss'
import { Run, Size } from '../../../types/ResponseTypes'
import { classToString, sizeToString } from '../../Common/StaticFunctionsTyped'
import { ListType, listTypeToString } from '../Turnament/PrintingDialog'

type Props = {
    type: ListType,
    run: Run,
    size: Size
}

const PageType = (props: Props) => {
    return (
        <Stack className={style.pagetypeContainer} justifyContent="center" alignItems="center">
            <Typography variant='h5'> {listTypeToString(props.type)} {classToString(props.run)} - {sizeToString(props.size)}</Typography>
        </Stack>
    )
}

export default PageType